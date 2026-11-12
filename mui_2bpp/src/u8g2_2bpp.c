/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  u8g2_2bpp.c: the minimal 2 bpp u8g2 (u8g2.h): setup, the roles of the
  tile map and the clut, the page buffer, boxes for the bar fields, text
  and controls from fonts.

  Coordinates are unsigned, as in u8g2; a value that wrapped below 0, such
  as x - padding at x = 0, is read as signed and clipped.

  Font format (tools/fontconv.py), multi-byte fields little-endian:
    header  0 format 1, 1 size, 2 ascent, 3 descent, 4 max advance, 5 0,
            6 glyph count (16 bits)
    index   per glyph 5 bytes: code point (16 bits), offset (24 bits), in
            code point order
    glyph   w, h, x (signed), y (signed, rows above the baseline), advance,
            then w * h pixels of 2 bits, 4 to a byte, first in bits 7:6
*/

#include <string.h>
#include "u8g2.h"
#include "u8x8_clut_driver.h"

#define FONT_HEADER 8
#define FONT_ENTRY 5

/*=== setup ===*/

static u8g2_uint_t u8g2_font_calc_vref_baseline(u8g2_t *u8g2)
{
  (void)u8g2;
  return 0;
}

void u8g2_SetupDisplay2bpp(u8g2_t *u8g2, uint8_t *buf, u8x8_msg_cb display_cb, u8x8_msg_cb cad_cb,
    u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb)
{
  u8x8_Setup(u8g2_GetU8x8(u8g2), display_cb, cad_cb, byte_cb, gpio_and_delay_cb);
  u8g2->buf = buf;
  u8g2->page_row = 0;
  u8g2->last_row = U8G2_TILE_ROWS - 1;
  u8g2->buf_row = 0;
  u8g2->font = NULL;
  u8g2->control_font = NULL;
  u8g2->font_ref_ascent = 0;
  u8g2->font_ref_descent = 0;
  u8g2->font_calc_vref = u8g2_font_calc_vref_baseline;
  u8g2->map = NULL;
  u8g2->clut = NULL;
}

void u8g2_Setup_st7789_240x280(u8g2_t *u8g2, uint8_t *buf, u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb)
{
  u8g2_SetupDisplay2bpp(u8g2, buf, u8x8_d_st7789_240x280, u8x8_cad_011, byte_cb, gpio_and_delay_cb);
}

void u8g2_SetClutTable2bpp(u8g2_t *u8g2, const uint32_t (*clut)[2])
{
  /* kept in the 1 bpp clut field of u8x8, which this library does not use */
  u8g2_GetU8x8(u8g2)->clut = (const uint16_t (*)[2])(const void *)clut;
}

const uint32_t (*u8g2_GetClutTable2bpp(u8x8_t *u8x8))[2]
{
  return (const uint32_t (*)[2])(const void *)u8x8->clut;
}

/*=== roles: the tile map and the clut ===*/

/* the tile map of the layout: each tile row the entry of its role */
static void u8g2_map_reset(u8g2_t *u8g2)
{
  uint16_t t;

  for ( t = 0; t < U8G2_TILE_COLS * U8G2_TILE_ROWS; t++ )
  {
    uint8_t r = (uint8_t)(t / U8G2_TILE_COLS);
    uint8_t role = U8G2_ROLE_CONTENT;

    if ( r < U8G2_STATUS_ROWS )
      role = U8G2_ROLE_STATUS;
    else if ( r < U8G2_STATUS_ROWS + U8G2_TITLE_ROWS )
      role = U8G2_ROLE_TITLE;
    else if ( r >= U8G2_TILE_ROWS - U8G2_BACK_ROWS )
      role = U8G2_ROLE_BACK;
    u8x8_clut_set_nibble(u8g2->map, t, role);
  }
}

void u8g2_SetupClut2bpp(u8g2_t *u8g2, uint8_t *map, uint32_t (*clut)[2])
{
  u8g2->map = map;
  u8g2->clut = clut;
  memset(clut, 0, 16 * sizeof clut[0]);
  u8g2_map_reset(u8g2);
  u8g2_SetClutMap(u8g2, map);
  u8g2_SetClutTable2bpp(u8g2, (const uint32_t (*)[2])clut);
}

/* the tiles of a rectangle; the edges round outward to tile edges, but a
   pixel of overhang is ignored (the padding of a bar of mui) */
static void u8g2_tile_span(u8g2_uint_t a, u8g2_uint_t len, int16_t lim, int16_t *t0, int16_t *t1)
{
  int16_t a0 = (int16_t)a, a1 = (int16_t)(a0 + (int16_t)len);

  if ( a0 < 0 ) a0 = 0;
  if ( a1 > lim ) a1 = lim;
  *t0 = (int16_t)((a0 + 1) / 8);
  *t1 = (int16_t)((a1 + 6) / 8);
}

void u8g2_MarkTiles(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h, uint8_t role)
{
  int16_t tx0, tx1, ty0, ty1, tx, ty;

  if ( u8g2->map == NULL )
    return;
  u8g2_tile_span(x, w, U8G2_DISPLAY_WIDTH, &tx0, &tx1);
  u8g2_tile_span(y, h, U8G2_DISPLAY_HEIGHT, &ty0, &ty1);
  for ( ty = ty0; ty < ty1; ty++ )
    for ( tx = tx0; tx < tx1; tx++ )
      u8x8_clut_set_nibble(u8g2->map, (uint16_t)(ty * U8G2_TILE_COLS + tx), role);
}

void u8g2_MarkBar(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h, uint8_t role)
{
  int16_t tx0, tx1, ty0, ty1, tx, ty;
  uint8_t corner;

  if ( u8g2->map == NULL )
    return;
  u8g2_tile_span(x, w, U8G2_DISPLAY_WIDTH, &tx0, &tx1);
  u8g2_tile_span(y, h, U8G2_DISPLAY_HEIGHT, &ty0, &ty1);
  if ( tx1 <= tx0 || ty1 - ty0 < 2 ||
      (role != U8G2_ROLE_INVERTED && role != U8G2_ROLE_EDIT && role != U8G2_ROLE_BUTTON) )
  {
    u8g2_MarkTiles(u8g2, x, y, w, h, role);
    return;
  }
  corner = (uint8_t)(role + 4);
  if ( tx0 > 0 )
    tx0--;
  if ( tx1 < U8G2_TILE_COLS )
    tx1++;
  for ( ty = ty0; ty < ty1; ty++ )
    for ( tx = tx0; tx < tx1; tx++ )
      u8x8_clut_set_nibble(u8g2->map, (uint16_t)(ty * U8G2_TILE_COLS + tx),
          (ty == ty0 || ty == ty1 - 1) && (tx == tx0 || tx == tx1 - 1) ? corner : role);
  /* the quarter discs: the center on the inner corner pixel of the tile,
     so that nothing is drawn outside it */
  u8g2_DrawDisc(u8g2, (u8g2_uint_t)(8 * tx0 + 7), (u8g2_uint_t)(8 * ty0 + 7), 7, U8G2_DRAW_UPPER_LEFT);
  u8g2_DrawDisc(u8g2, (u8g2_uint_t)(8 * (tx1 - 1)), (u8g2_uint_t)(8 * ty0 + 7), 7, U8G2_DRAW_UPPER_RIGHT);
  u8g2_DrawDisc(u8g2, (u8g2_uint_t)(8 * tx0 + 7), (u8g2_uint_t)(8 * (ty1 - 1)), 7, U8G2_DRAW_LOWER_LEFT);
  u8g2_DrawDisc(u8g2, (u8g2_uint_t)(8 * (tx1 - 1)), (u8g2_uint_t)(8 * (ty1 - 1)), 7, U8G2_DRAW_LOWER_RIGHT);
}

/* the tile rows u8g2_MarkTiles() marks for the pixel rows y .. y+h-1;
   none: *r0 > *r1 */
void u8g2_GetTileRows(u8g2_t *u8g2, u8g2_uint_t y, u8g2_uint_t h, uint8_t *r0, uint8_t *r1)
{
  int16_t t0, t1;

  (void)u8g2;
  u8g2_tile_span(y, h, U8G2_DISPLAY_HEIGHT, &t0, &t1);
  if ( t1 <= t0 )
  {
    *r0 = 1;
    *r1 = 0;
    return;
  }
  *r0 = (uint8_t)t0;
  *r1 = (uint8_t)(t1 - 1);
}

/*=== page buffer ===*/

void u8g2_ClearBuffer(u8g2_t *u8g2)
{
  memset(u8g2->buf, 0, U8G2_BUF_SIZE);
}

/* one past the last tile row of the pass: the end of the buffer, at most
   the last row of the update */
static uint8_t u8g2_page_end(u8g2_t *u8g2)
{
  uint16_t end = (uint16_t)(u8g2->buf_row + U8G2_PAGE_ROWS);

  if ( end > u8g2->last_row + 1u )
    end = (uint16_t)(u8g2->last_row + 1u);
  return (uint8_t)end;
}

/* the address of tile row r in the buffer */
static uint8_t *u8g2_row_ptr(u8g2_t *u8g2, uint8_t r)
{
  return u8g2->buf + (uint16_t)(r - u8g2->buf_row) * 8 * U8G2_LINE_STRIDE;
}

/* clear the tile rows of the pass; with a full buffer the other rows keep
   the screen */
static void u8g2_clear_pass(u8g2_t *u8g2)
{
  uint8_t end = u8g2_page_end(u8g2);

  memset(u8g2_row_ptr(u8g2, u8g2->page_row), 0,
      (size_t)(end - u8g2->page_row) * 8 * U8G2_LINE_STRIDE);
}

void u8g2_FirstRows(u8g2_t *u8g2, uint8_t first, uint8_t last)
{
  u8g2->page_row = first;
  u8g2->last_row = last;
  /* a full buffer is the screen: the rows are drawn in place */
  u8g2->buf_row = (U8G2_PAGE_ROWS < U8G2_TILE_ROWS) ? first : 0;
  u8g2_clear_pass(u8g2);
  /* the bars of mui mark their tiles while the first pass is drawn, before
     it is sent: mui draws all fields on every pass, and u8g2_MarkTiles()
     marks all of the map, not only the rows of the pass */
  if ( u8g2->map != NULL )
    u8g2_map_reset(u8g2);
}

void u8g2_FirstPage(u8g2_t *u8g2)
{
  u8g2_FirstRows(u8g2, 0, U8G2_TILE_ROWS - 1);
}

/* send the tile rows of the pass, one u8x8 tile row at a time; the stream
   function finds the pixel rows of a tile U8G2_LINE_STRIDE apart */
static void u8g2_send_page(u8g2_t *u8g2)
{
  uint8_t end = u8g2_page_end(u8g2);
  uint8_t r;

  for ( r = u8g2->page_row; r < end; r++ )
    u8x8_DrawTile(u8g2_GetU8x8(u8g2), 0, r, U8G2_TILE_COLS, u8g2_row_ptr(u8g2, r));
}

uint8_t u8g2_NextPage(u8g2_t *u8g2)
{
  uint8_t end = u8g2_page_end(u8g2);

  u8g2_send_page(u8g2);
  if ( end > u8g2->last_row )
  {
    u8g2->page_row = 0;
    u8g2->last_row = U8G2_TILE_ROWS - 1;
    u8g2->buf_row = 0;
    u8x8_RefreshDisplay(u8g2_GetU8x8(u8g2));
    return 0;
  }
  u8g2->page_row = end;
  if ( U8G2_PAGE_ROWS < U8G2_TILE_ROWS )
    u8g2->buf_row = end;
  u8g2_clear_pass(u8g2);
  return 1;
}

/* all tile rows of the panel, also those below the screen of the library */
void u8g2_ClearDisplay(u8g2_t *u8g2)
{
  u8x8_t *u8x8 = u8g2_GetU8x8(u8g2);
  uint8_t r;

  u8g2_ClearBuffer(u8g2);
  for ( r = 0; r < u8x8->display_info->tile_height; r++ )
    u8x8_DrawTile(u8x8, 0, r, U8G2_TILE_COLS, u8g2->buf);
}

/*=== boxes, for the bar fields of mui ===*/

/* the pixel rows of the pass: [*y0, *y1) */
static void u8g2_page_rows(u8g2_t *u8g2, int16_t *y0, int16_t *y1)
{
  *y0 = (int16_t)(u8g2->page_row * 8);
  *y1 = (int16_t)(u8g2_page_end(u8g2) * 8);
}

/* the address of pixel row y in the buffer */
static uint8_t *u8g2_pixel_row(u8g2_t *u8g2, int16_t y)
{
  return u8g2->buf + (uint16_t)(y - u8g2->buf_row * 8) * U8G2_LINE_STRIDE;
}

/* a pixel of level 0 .. 3, clipped to the screen and the page; it takes
   the larger of its level and the new one, as a glyph pixel */
void u8g2_DrawPixelLevel(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, uint8_t level)
{
  int16_t xs = (int16_t)x, ys = (int16_t)y, p0, p1;
  uint8_t *p, sh;

  if ( level == 0 || xs < 0 || xs >= U8G2_DISPLAY_WIDTH )
    return;
  u8g2_page_rows(u8g2, &p0, &p1);
  if ( ys < p0 || ys >= p1 )
    return;
  if ( level > 3 )
    level = 3;
  p = u8g2_pixel_row(u8g2, ys) + (xs >> 2);
  sh = (uint8_t)(6 - 2 * (xs & 3));
  if ( ((*p >> sh) & 3) < level )
    *p = (uint8_t)((*p & ~(3 << sh)) | level << sh);
}

/* pixels x0 .. x1-1 of a row set to 3, x0 < x1 */
static void u8g2_fill_row(uint8_t *row, int16_t x0, int16_t x1)
{
  int16_t b0 = x0 >> 2;
  int16_t b1 = (x1 - 1) >> 2;
  uint8_t m0 = (uint8_t)(0xFF >> (2 * (x0 & 3)));
  uint8_t m1 = (uint8_t)(0xFF << (2 * (3 - ((x1 - 1) & 3))));

  if ( b0 == b1 )
  {
    row[b0] |= (uint8_t)(m0 & m1);
    return;
  }
  row[b0] |= m0;
  memset(row + b0 + 1, 0xFF, (size_t)(b1 - b0 - 1));
  row[b1] |= m1;
}

void u8g2_DrawBox(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h)
{
  int16_t x0 = (int16_t)x, x1 = (int16_t)(x0 + (int16_t)w);
  int16_t y0 = (int16_t)y, y1 = (int16_t)(y0 + (int16_t)h);
  int16_t p0, p1, yy;

  u8g2_page_rows(u8g2, &p0, &p1);
  if ( x0 < 0 ) x0 = 0;
  if ( x1 > U8G2_DISPLAY_WIDTH ) x1 = U8G2_DISPLAY_WIDTH;
  if ( y0 < p0 ) y0 = p0;
  if ( y1 > p1 ) y1 = p1;
  if ( x0 >= x1 || y0 >= y1 )
    return;
  for ( yy = y0; yy < y1; yy++ )
    u8g2_fill_row(u8g2_pixel_row(u8g2, yy), x0, x1);
}

void u8g2_DrawFrame(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h)
{
  if ( w == 0 || h == 0 )
    return;
  u8g2_DrawBox(u8g2, x, y, w, 1);
  u8g2_DrawBox(u8g2, x, (u8g2_uint_t)(y + h - 1), w, 1);
  u8g2_DrawBox(u8g2, x, y, 1, h);
  u8g2_DrawBox(u8g2, (u8g2_uint_t)(x + w - 1), y, 1, h);
}

/*=== text ===*/

void u8g2_SetFont(u8g2_t *u8g2, const uint8_t *font)
{
  u8g2->font = font;
  u8g2->font_ref_ascent = (int8_t)font[2];
  u8g2->font_ref_descent = (int8_t)-font[3];
}

/* the glyph of code point cp, NULL if the font does not have it */
static const uint8_t *u8g2_find_glyph(const uint8_t *font, uint16_t cp)
{
  uint16_t lo = 0, hi = (uint16_t)(font[6] | font[7] << 8);

  while ( lo < hi )
  {
    uint16_t mid = (uint16_t)((lo + hi) / 2);
    const uint8_t *e = font + FONT_HEADER + (uint32_t)mid * FONT_ENTRY;
    uint16_t c = (uint16_t)(e[0] | e[1] << 8);

    if ( c == cp )
      return font + (e[2] | (uint32_t)e[3] << 8 | (uint32_t)e[4] << 16);
    if ( c < cp )
      lo = (uint16_t)(mid + 1);
    else
      hi = mid;
  }
  return NULL;
}

/* a pixel takes the larger of its level and the level of the glyph */
static void u8g2_draw_glyph(u8g2_t *u8g2, int16_t x, int16_t y, const uint8_t *g)
{
  uint8_t w = g[0], h = g[1];
  int16_t left = (int16_t)(x + (int8_t)g[2]);
  int16_t top = (int16_t)(y - (int8_t)g[3]);
  const uint8_t *px = g + 5;
  int16_t p0, p1, i0, i1, i, j;

  u8g2_page_rows(u8g2, &p0, &p1);
  i0 = (int16_t)(p0 - top);
  i1 = (int16_t)(p1 - top);
  if ( i0 < 0 ) i0 = 0;
  if ( i1 > h ) i1 = h;
  for ( i = i0; i < i1; i++ )
  {
    uint8_t *row = u8g2_pixel_row(u8g2, (int16_t)(top + i));
    for ( j = 0; j < w; j++ )
    {
      uint16_t k = (uint16_t)(i * w + j);
      uint8_t v = (uint8_t)((px[k >> 2] >> (6 - 2 * (k & 3))) & 3);
      int16_t col = (int16_t)(left + j);
      uint8_t *p, sh;

      if ( v == 0 || col < 0 || col >= U8G2_DISPLAY_WIDTH )
        continue;
      p = row + (col >> 2);
      sh = (uint8_t)(6 - 2 * (col & 3));
      if ( ((*p >> sh) & 3) < v )
        *p = (uint8_t)((*p & ~(3 << sh)) | v << sh);
    }
  }
}

/* the next code point of a string, 0 at its end; utf8 = 0: a byte is a code point */
static uint16_t u8g2_next(const char **s, uint8_t utf8)
{
  const uint8_t *p = (const uint8_t *)*s;
  uint16_t cp;
  uint8_t n;

  for (;;)
  {
    cp = *p;
    if ( cp == 0 )
      return 0;
    p++;
    if ( !utf8 || cp < 0x80 )
      break;
    if ( cp >= 0xE0 && cp < 0xF0 )
    {
      cp &= 0x0F;
      n = 2;
    }
    else if ( cp >= 0xC0 && cp < 0xE0 )
    {
      cp &= 0x1F;
      n = 1;
    }
    else
      continue;                 /* a continuation byte, or beyond 16 bits: skip */
    while ( n-- && (*p & 0xC0) == 0x80 )
      cp = (uint16_t)(cp << 6 | (*p++ & 0x3F));
    break;
  }
  *s = (const char *)p;
  return cp;
}

static u8g2_uint_t u8g2_draw_string(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, const char *str, uint8_t utf8)
{
  int16_t xx = (int16_t)x;
  int16_t yy = (int16_t)(y + u8g2->font_calc_vref(u8g2));
  uint16_t cp;

  while ( (cp = u8g2_next(&str, utf8)) != 0 )
  {
    const uint8_t *g = u8g2_find_glyph(u8g2->font, cp);
    if ( g == NULL )
      continue;
    u8g2_draw_glyph(u8g2, xx, yy, g);
    xx = (int16_t)(xx + g[4]);
  }
  return (u8g2_uint_t)(xx - (int16_t)x);
}

/* as u8g2: the advances, but of the last glyph its x offset and width */
static u8g2_uint_t u8g2_string_width(u8g2_t *u8g2, const char *str, uint8_t utf8)
{
  const uint8_t *last = NULL;
  u8g2_uint_t w = 0;
  uint16_t cp;

  while ( (cp = u8g2_next(&str, utf8)) != 0 )
  {
    const uint8_t *g = u8g2_find_glyph(u8g2->font, cp);
    if ( g == NULL )
      continue;
    w = (u8g2_uint_t)(w + g[4]);
    last = g;
  }
  if ( last != NULL && last[0] != 0 )
    w = (u8g2_uint_t)(w - last[4] + (int8_t)last[2] + last[0]);
  return w;
}

u8g2_uint_t u8g2_DrawStr(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, const char *str)
{
  return u8g2_draw_string(u8g2, x, y, str, 0);
}

u8g2_uint_t u8g2_DrawUTF8(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, const char *str)
{
  return u8g2_draw_string(u8g2, x, y, str, 1);
}

u8g2_uint_t u8g2_GetStrWidth(u8g2_t *u8g2, const char *str)
{
  return u8g2_string_width(u8g2, str, 0);
}

u8g2_uint_t u8g2_GetUTF8Width(u8g2_t *u8g2, const char *str)
{
  return u8g2_string_width(u8g2, str, 1);
}

/*=== controls ===*/

void u8g2_SetControlFont(u8g2_t *u8g2, const uint8_t *font)
{
  u8g2->control_font = font;
}

/* the ink of the glyph centred in the box x .. x+w-1, y-w .. y-1 */
void u8g2_DrawControl(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, uint16_t cp)
{
  const uint8_t *g;
  int16_t left, top;

  if ( u8g2->control_font == NULL || (g = u8g2_find_glyph(u8g2->control_font, cp)) == NULL )
    return;
  left = (int16_t)((int16_t)x + ((int16_t)w - g[0]) / 2);
  top = (int16_t)((int16_t)y - (int16_t)w + ((int16_t)w - g[1]) / 2);
  u8g2_draw_glyph(u8g2, (int16_t)(left - (int8_t)g[2]), (int16_t)(top + (int8_t)g[3]), g);
}
