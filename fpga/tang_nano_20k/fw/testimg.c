/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/* testimg.c: test images in fbmem, and the requests that send them */

#include <stdint.h>
#include <string.h>
#include "u8g2.h"
#include "mui.h"
#include "mui_u8g2.h"
#include "testimg.h"

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint16_t rgb444(unsigned r, unsigned g, unsigned b)
{
  return (uint16_t)(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
}

static void setpx(volatile uint8_t *fb, unsigned x, unsigned y)
{
  fb[y * COLS + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}

static void clear(volatile uint8_t *mem)
{
  unsigned t;

  for (t = 0; t < COLS * LCD_H; t++)
    mem[FB_OFS + t] = 0;
  for (t = 0; t < (COLS * ROWS + 1) / 2; t++)
    mem[MAP_OFS + t] = 0;
}

static void setidx(volatile uint8_t *mem, unsigned x, unsigned y, unsigned idx)
{
  unsigned t = y * COLS + x;

  mem[MAP_OFS + t / 2] |= (uint8_t)(idx << (4 * (t & 1)));
}

/* bitmap: border, grid every 40 pixels, two diagonals. map: diagonal bands of
   the 16 entries. clut: bright foreground, dark background, 8 hues in two
   brightnesses; clut2 the same with fg and bg swapped; clut4 in rgb444 */
void pattern(volatile uint8_t *mem)
{
  volatile uint8_t *fb = mem + FB_OFS;
  volatile uint32_t *clut = (volatile uint32_t *)(mem + CLUT_OFS);
  volatile uint32_t *clut2 = (volatile uint32_t *)(mem + CLUT2_OFS);
  volatile uint32_t *clut4 = (volatile uint32_t *)(mem + CLUT4_OFS);
  unsigned x, y, i;

  clear(mem);
  for (y = 0; y < LCD_H; y++)
    for (x = 0; x < LCD_W; x++) {
      unsigned d = y * LCD_W / LCD_H;
      if (x == 0 || x == LCD_W - 1 || y == 0 || y == LCD_H - 1 ||
          x % 40 == 0 || y % 40 == 0 || x == d || x == LCD_W - 1 - d)
        setpx(fb, x, y);
    }

  for (y = 0; y < ROWS; y++)
    for (x = 0; x < COLS; x++)
      setidx(mem, x, y, (x + y) & 15);

  for (i = 0; i < 16; i++) {
    unsigned hi = (i & 8) ? 255 : 160, lo = (i & 8) ? 64 : 24;
    unsigned fr = (i & 4) ? hi : 48, fg = (i & 2) ? hi : 48, fb_ = (i & 1) ? hi : 48;
    unsigned br = (i & 4) ? lo : 0, bg = (i & 2) ? lo : 0, bb = (i & 1) ? lo : 0;
    uint16_t f = rgb565(fr, fg, fb_), b = rgb565(br, bg, bb);
    clut[i]  = ((uint32_t)b << 16) | f;
    clut2[i] = ((uint32_t)f << 16) | b;
    clut4[i] = ((uint32_t)rgb444(br, bg, bb) << 16) | rgb444(fr, fg, fb_);
  }
}

/* border test: a line on the outermost pixels, one color per side: top red,
   right green, bottom blue, left yellow (top and bottom win in the corners).
   the top left tile, pixel (0, 0), is a white block. black elsewhere */
void border(volatile uint8_t *mem)
{
  volatile uint8_t *fb = mem + FB_OFS;
  volatile uint32_t *clut = (volatile uint32_t *)(mem + CLUT3_OFS);
  static const uint16_t fg[6] = { 0x0000, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xFFFF };
  unsigned x, y, i;

  clear(mem);
  for (x = 0; x < LCD_W; x++) {
    setpx(fb, x, 0);
    setpx(fb, x, LCD_H - 1);
  }
  for (y = 0; y < LCD_H; y++) {
    setpx(fb, 0, y);
    setpx(fb, LCD_W - 1, y);
  }
  for (y = 0; y < 8; y++)
    fb[y * COLS] = 0xFF;            /* top left tile */

  for (y = 0; y < ROWS; y++)
    for (x = 0; x < COLS; x++) {
      unsigned idx = 0;
      if (x == 0) idx = 4;          /* left: yellow */
      if (x == COLS - 1) idx = 2;   /* right: green */
      if (y == 0) idx = 1;          /* top: red */
      if (y == ROWS - 1) idx = 3;   /* bottom: blue */
      if (x == 0 && y == 0) idx = 5; /* origin: white */
      setidx(mem, x, y, idx);
    }

  for (i = 0; i < 16; i++)
    clut[i] = i < 6 ? fg[i] : 0;    /* background black */
}

/* ------------------------------------------------------------------ */
/* u8g2: the clut examples of the u8g2 clut branch, drawn by u8g2 in a buffer
   in horizontal layout in fbmem, tile clut map in fbmem. the display driver
   u8x8_d_clut_dma sends tile rows with clut_dma; the st7789 driver of u8g2
   gives the display size and the default palette (rgb444) */

static u8g2_t u8g2;
static volatile uint8_t *drv_mem;     /* fbmem, for u8x8_d_clut_dma */
static demo_send drv_send;

static uint8_t u8x8_none(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  (void)u8x8; (void)msg; (void)arg_int; (void)arg_ptr;
  return 1;
}

/* the clut of u8g2, uint16_t [16][2] foreground, background: the layout of
   the clut_dma clut */
static void u8g2_clut(volatile uint8_t *mem)
{
  memcpy((uint8_t *)(uintptr_t)(mem + CLUT_OFS), u8g2.u8x8.clut, sizeof u8g2.u8x8.clut);
}

/* u8x8 display driver: a tile row of the buffer to the display with clut_dma.
   the address comes from the tile position, not from the tile pointer: u8g2
   computes that pointer for the vertical layout. in the page buffer modes the
   buffer holds the tile rows from u8g2->tile_curr_row on */
static uint8_t u8x8_d_clut_dma(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  switch (msg) {
  case U8X8_MSG_DISPLAY_SETUP_MEMORY:
    return u8x8_d_st7789_240x280(u8x8, msg, arg_int, arg_ptr);
  case U8X8_MSG_DISPLAY_DRAW_TILE: {
    const u8g2_t *u = (const u8g2_t *)u8x8;   /* u8x8 is the first member */
    const u8x8_tile_t *t = (const u8x8_tile_t *)arg_ptr;
    uint16_t row = (uint16_t)(t->y_pos - u->tile_curr_row);

    u8g2_clut(drv_mem);
    drv_send((uint16_t)(FB_OFS + row * 8u * u8x8->display_info->tile_width + t->x_pos),
             t->x_pos, t->y_pos, t->cnt, 1, "tile row");
    return 1;
  }
  default:
    return 1;
  }
}

/* u8g2 with a buffer of rows tile rows in fbmem */
static u8g2_t *u8g2_fbmem(volatile uint8_t *mem, uint8_t rows)
{
  uint8_t *m = (uint8_t *)(uintptr_t)mem;

  drv_mem = mem;
  u8g2_SetupDisplay(&u8g2, u8x8_d_clut_dma, u8x8_cad_001, u8x8_none, u8x8_none);
  u8g2_SetupBuffer(&u8g2, m + FB_OFS, rows, u8g2_ll_hvline_horizontal_right_lsb, U8G2_R0);
  memset(m + MAP_OFS, 0, u8x8_clut_map_size(LCD_W, LCD_H));
  u8g2_SetClutMap(&u8g2, m + MAP_OFS);
  return &u8g2;
}

/* a rectangle of tiles of the full buffer */
static void send_area(demo_send send, uint8_t tx, uint8_t ty, uint8_t tw, uint8_t th, const char *what)
{
  send((uint16_t)(FB_OFS + 8u * ty * COLS + tx), tx, ty, tw, th, what);
}

/* ClutPalette.ino: 2x8 grid, one cell per clut entry, with its name */
void palette(volatile uint8_t *mem)
{
  static const char *const color_name[16] = {
    "BLK", "BLU", "GRN", "CYN", "RED", "MAG", "BRN", "LGY",
    "DGY", "LBU", "LGN", "LCY", "LRD", "LMG", "YEL", "WHT"
  };
  u8g2_t *u = u8g2_fbmem(mem, ROWS);
  uint8_t cell_tw = u8x8_GetCols(u8g2_GetU8x8(u)) / 2;
  uint8_t cell_th = u8x8_GetRows(u8g2_GetU8x8(u)) / 8;
  uint8_t i;

  u8g2_SetFont(u, u8g2_font_profont22_tr);
  u8g2_ClearBuffer(u);
  for (i = 0; i < 16; i++) {
    uint8_t col = i / 8, row = i % 8;
    uint8_t tx0 = col * cell_tw, ty0 = row * cell_th;
    uint16_t px0 = tx0 * 8, py0 = ty0 * 8, w = cell_tw * 8, h = cell_th * 8;
    uint16_t str_w = u8g2_GetStrWidth(u, color_name[i]);
    int16_t baseline = (int16_t)py0 + (h + u8g2_GetAscent(u) + u8g2_GetDescent(u)) / 2;

    u8g2_SetClutRegion(u, tx0, ty0, cell_tw, cell_th, i);
    u8g2_DrawStr(u, (int16_t)(px0 + (w - str_w) / 2), baseline, color_name[i]);
  }
  u8g2_clut(mem);
}

/* ClutColor.ino: dashboard of 4 strips, header SOLAR CELL and TEMP, SUN,
   LOAD. a value that crosses a threshold changes the clut entry of its strip
   and sends the whole strip; otherwise only the tiles of the value are sent */

#define CC_ROWS 4                 /* strip 0 = header, 1..3 = TEMP, SUN, LOAD */
#define CC_GAP 1                  /* tile rows of black between strips */
#define CC_VALUE_CHARS 6          /* reserved width of a value, in characters */

#define COLOR_GREEN  0x00FF00
#define COLOR_YELLOW 0xFFFF00
#define COLOR_RED    0xFF0000
#define COLOR_BLACK  0x000000
#define COLOR_WHITE  0xFFFFFF
#define COLOR_BLUE   0x0000AA

static uint8_t cc_ty0[CC_ROWS], cc_th, cc_tw;
static uint8_t cc_bucket[CC_ROWS];
static int8_t cc_v[3], cc_d[3];

static uint8_t cc_threshold(int8_t v)
{
  if (v <= 20) return 0;          /* green */
  if (v <= 40) return 1;          /* yellow */
  return 2;                       /* red */
}

static void cc_color(uint8_t bucket, uint32_t *fg, uint32_t *bg)
{
  switch (bucket) {
  case 0:  *fg = COLOR_BLACK; *bg = COLOR_GREEN; break;
  case 1:  *fg = COLOR_BLACK; *bg = COLOR_YELLOW; break;
  default: *fg = COLOR_WHITE; *bg = COLOR_RED; break;
  }
}

/* "%3d%s" */
static void cc_format(char *s, int v, const char *unit)
{
  char t[4];
  int n = 0, neg = v < 0;
  unsigned u = neg ? (unsigned)-v : (unsigned)v;

  do
    t[n++] = (char)('0' + u % 10);
  while ((u /= 10) != 0);
  if (neg)
    t[n++] = '-';
  for (int pad = 3 - n; pad > 0; pad--)
    *s++ = ' ';
  while (n)
    *s++ = t[--n];
  while (*unit)
    *s++ = *unit++;
  *s = 0;
}

/* the clut to fbmem, then the update */
static void cc_send(volatile uint8_t *mem, demo_send send, uint8_t tx, uint8_t ty, uint8_t tw, uint8_t th, const char *what)
{
  u8g2_clut(mem);
  send_area(send, tx, ty, tw, th, what);
}

/* redraws only the value of a strip */
static void cc_value(volatile uint8_t *mem, demo_send send, uint8_t row, int8_t value, const char *unit, uint8_t idx)
{
  u8g2_t *u = &u8g2;
  uint16_t py0 = cc_ty0[row] * 8;
  uint16_t w = cc_tw * 8, h = cc_th * 8, half_h = h / 2;
  uint16_t value_w, value_x0, str_w;
  int16_t line2_y;
  uint8_t bucket;
  char s[16];

  u8g2_SetFont(u, u8g2_font_profont22_tf);
  value_w = (uint16_t)(CC_VALUE_CHARS * u8g2_GetMaxCharWidth(u));
  value_x0 = (w - value_w) / 2;

  u8g2_SetDrawColor(u, 0);
  u8g2_DrawBox(u, value_x0, py0 + half_h, value_w, half_h);
  u8g2_SetDrawColor(u, 1);

  cc_format(s, value, unit);
  str_w = u8g2_GetUTF8Width(u, s);
  line2_y = (int16_t)py0 + half_h + (half_h + u8g2_GetAscent(u) + u8g2_GetDescent(u)) / 2;
  u8g2_DrawUTF8(u, (w - str_w) / 2, line2_y, s);

  bucket = cc_threshold(value);
  if (bucket != cc_bucket[row]) {
    uint32_t fg, bg;
    cc_bucket[row] = bucket;
    cc_color(bucket, &fg, &bg);
    u8g2_SetClutColor(u, idx, fg, bg);
    cc_send(mem, send, 0, cc_ty0[row], cc_tw, cc_th, "strip");
  } else {
    uint8_t tx0 = (uint8_t)(value_x0 / 8);
    uint8_t tx1 = (uint8_t)((value_x0 + value_w + 7) / 8);
    uint8_t ty0 = (uint8_t)((py0 + half_h) / 8);
    uint8_t ty1 = (uint8_t)((py0 + h + 7) / 8);
    cc_send(mem, send, tx0, ty0, (uint8_t)(tx1 - tx0), (uint8_t)(ty1 - ty0), "value");
  }
}

void clutcolor_init(volatile uint8_t *mem, demo_send send)
{
  static const char *const labels[CC_ROWS] = { 0, "TEMP", "SUN", "LOAD" };
  u8g2_t *u = u8g2_fbmem(mem, ROWS);
  uint16_t w, h, half_h, str_w;
  uint8_t row;
  const char *title = "SOLAR CELL";

  cc_tw = u8x8_GetCols(u8g2_GetU8x8(u));
  cc_th = (u8x8_GetRows(u8g2_GetU8x8(u)) - (CC_ROWS - 1) * CC_GAP) / CC_ROWS;
  for (row = 0; row < CC_ROWS; row++) {
    cc_ty0[row] = row * (cc_th + CC_GAP);
    u8g2_SetClutRegion(u, 0, cc_ty0[row], cc_tw, cc_th, row + 1);
  }
  for (row = 0; row < 3; row++) {
    cc_v[row] = 0;
    cc_d[row] = (int8_t)(row == 0 ? 2 : row == 1 ? 3 : 5);
  }

  u8g2_ClearBuffer(u);

  /* header: drawn once */
  u8g2_SetFont(u, u8g2_font_profont22_tf);
  w = cc_tw * 8;
  h = cc_th * 8;
  str_w = u8g2_GetStrWidth(u, title);
  u8g2_DrawStr(u, (w - str_w) / 2, (int16_t)(h + u8g2_GetAscent(u) + u8g2_GetDescent(u)) / 2, title);
  u8g2_SetClutColor(u, 1, COLOR_WHITE, COLOR_BLUE);

  /* strip labels: drawn once */
  half_h = h / 2;
  u8g2_SetFont(u, u8g2_font_profont17_tf);
  for (row = 1; row < CC_ROWS; row++) {
    uint16_t py0 = cc_ty0[row] * 8;
    uint32_t fg, bg;
    u8g2_DrawUTF8(u, 4, (int16_t)py0 + (half_h + u8g2_GetAscent(u) + u8g2_GetDescent(u)) / 2, labels[row]);
    cc_bucket[row] = cc_threshold(0);
    cc_color(cc_bucket[row], &fg, &bg);
    u8g2_SetClutColor(u, row + 1, fg, bg);
  }

  cc_send(mem, send, 0, 0, cc_tw, u8x8_GetRows(u8g2_GetU8x8(u)), "screen");
}

void clutcolor_tick(volatile uint8_t *mem, demo_send send)
{
  uint8_t i;

  for (i = 0; i < 3; i++) {
    cc_v[i] = (int8_t)(cc_v[i] + cc_d[i]);
    if (cc_v[i] <= 0 || cc_v[i] >= 60)
      cc_d[i] = (int8_t)-cc_d[i];
  }
  cc_value(mem, send, 1, cc_v[0], "\xc2\xb0" "C", 2);
  cc_value(mem, send, 2, cc_v[1], "%", 3);
  cc_value(mem, send, 3, cc_v[2], "%", 4);
}

/* ClutClock.ino: analog clock. tiles around the edge get CLUT_RING, whose
   color changes every second, the tiles inside get CLUT_HANDS. starts at a
   fixed time instead of a random one, so the hardware and the simulation show
   the same */

#define R_TICK_OUTER  116          /* dial geometry, pixels from center */
#define R_TICK_MINUTE 110
#define R_TICK_HOUR   101
#define R_NUMERAL     88
#define R_RING_ZONE   80           /* tiles with center beyond this radius get the ring color */
#define L_HOUR        50
#define L_MINUTE      78
#define L_SECOND      94
#define L_SECOND_TAIL 16
#define W_HOUR        4            /* half width at the base */
#define W_MINUTE      3
#define R_HUB         4

#define CLUT_RING  1
#define CLUT_HANDS 2

#define COLOR_CYAN    0x00C0FF

#define CK_START (10u * 3600u + 8u * 60u + 30u)   /* 10:08:30 */

static int16_t ck_cx, ck_cy;
static uint32_t ck_secs;

/* smoothly changing color, a cycle every minute */
static uint32_t rgb_cycle(uint32_t t)
{
  uint32_t segment, x, r, g, b;

  t = t * 1536 / 60;
  t %= 1536;
  segment = t >> 8;
  x = t & 255;
  switch (segment) {
  case 0:  r = 255;     g = x;       b = 0;       break;   /* red -> yellow */
  case 1:  r = 255 - x; g = 255;     b = 0;       break;   /* yellow -> green */
  case 2:  r = 0;       g = 255;     b = x;       break;   /* green -> cyan */
  case 3:  r = 0;       g = 255 - x; b = 255;     break;   /* cyan -> blue */
  case 4:  r = x;       g = 0;       b = 255;     break;   /* blue -> magenta */
  default: r = 255;     g = 0;       b = 255 - x; break;   /* magenta -> red */
  }
  return (r << 16) | (g << 8) | b;
}

/* sin and cos of positions 0..7 (0..42 degrees), scaled by 1024 */
static const int16_t sin_table[8] = { 0, 107, 213, 316, 416, 512, 602, 685 };
static const int16_t cos_table[8] = { 1024, 1018, 1002, 974, 935, 887, 828, 761 };

/* sin and cos of position pos (0..59, clockwise from 12), scaled by 1024 */
static void sin_cos(uint8_t pos, int16_t *s, int16_t *c)
{
  uint8_t q = pos / 15, r = pos % 15;
  int16_t a, b;                   /* sin and cos within the first quadrant */

  if (r <= 7) {
    a = sin_table[r];
    b = cos_table[r];
  } else {                        /* sin(90 - x) = cos x */
    a = cos_table[15 - r];
    b = sin_table[15 - r];
  }
  switch (q) {                    /* sin(x + 90) = cos x, cos(x + 90) = -sin x */
  case 0:  *s = a;  *c = b;  break;
  case 1:  *s = b;  *c = -a; break;
  case 2:  *s = -a; *c = -b; break;
  default: *s = -b; *c = a;  break;
  }
}

/* v / 1024, rounded */
static int16_t descale(int32_t v)
{
  return (int16_t)((v + 512) >> 10);
}

/* point at radius r at position pos, shifted sideways by off pixels */
static void polar(uint8_t pos, int16_t r, int16_t off, int16_t *x, int16_t *y)
{
  int16_t s, c;

  sin_cos(pos, &s, &c);
  *x = ck_cx + descale((int32_t)r * s + (int32_t)off * c);
  *y = ck_cy + descale(-(int32_t)r * c + (int32_t)off * s);
}

static void draw_radial(uint8_t pos, int16_t r0, int16_t r1, int16_t off)
{
  int16_t x0, y0, x1, y1;

  polar(pos, r0, off, &x0, &y0);
  polar(pos, r1, off, &x1, &y1);
  u8g2_DrawLine(&u8g2, x0, y0, x1, y1);
}

/* watch hand: tip at length len, base of half width w at the center */
static void draw_hand(uint8_t pos, int16_t len, int16_t w)
{
  int16_t xt, yt, xl, yl, xr, yr;

  polar(pos, len, 0, &xt, &yt);
  polar(pos, 0, -w, &xl, &yl);
  polar(pos, 0, w, &xr, &yr);
  u8g2_DrawTriangle(&u8g2, xt, yt, xl, yl, xr, yr);
}

static void draw_dial(void)
{
  static const char *const numeral[4] = { "12", "3", "6", "9" };
  int16_t ascent;
  uint8_t i;
  int8_t off;

  for (i = 0; i < 60; i++) {
    if (i % 5 == 0) {
      for (off = -1; off <= 1; off++)
        draw_radial(i, R_TICK_HOUR, R_TICK_OUTER, off);
    } else
      draw_radial(i, R_TICK_MINUTE, R_TICK_OUTER, 0);
  }
  u8g2_SetFont(&u8g2, u8g2_font_profont17_tn);
  ascent = u8g2_GetAscent(&u8g2);
  for (i = 0; i < 4; i++) {
    int16_t x, y;
    polar(i * 15, R_NUMERAL, 0, &x, &y);
    u8g2_DrawStr(&u8g2, x - u8g2_GetStrWidth(&u8g2, numeral[i]) / 2, y + ascent / 2, numeral[i]);
  }
}

/* picture loop: in the page buffer modes the clock is drawn once per page */
static void draw_clock(volatile uint8_t *mem, demo_send send, uint32_t secs)
{
  uint8_t hours = (secs / 3600) % 24;
  uint8_t minutes = (secs / 60) % 60;
  uint8_t seconds = secs % 60;
  uint8_t hour_pos = (hours % 12) * 5 + minutes / 12;

  u8g2_SetClutColor(&u8g2, CLUT_RING, rgb_cycle(secs), COLOR_BLACK);
  drv_mem = mem;
  drv_send = send;
  u8g2_FirstPage(&u8g2);
  do {
    draw_dial();
    draw_hand(hour_pos, L_HOUR, W_HOUR);
    draw_hand(minutes, L_MINUTE, W_MINUTE);
    draw_radial(seconds, -L_SECOND_TAIL, L_SECOND, 0);
    u8g2_DrawDisc(&u8g2, ck_cx, ck_cy, R_HUB, U8G2_DRAW_ALL);
  } while (u8g2_NextPage(&u8g2));
}

void clutclock_init(volatile uint8_t *mem, demo_send send)
{
  u8g2_t *u = u8g2_fbmem(mem, PAGE_ROWS);
  uint8_t cols = u8x8_GetCols(u8g2_GetU8x8(u)), rows = u8x8_GetRows(u8g2_GetU8x8(u));
  uint8_t tx, ty;

  u8g2_SetClutColor(u, CLUT_RING, COLOR_CYAN, COLOR_BLACK);
  u8g2_SetClutColor(u, CLUT_HANDS, COLOR_WHITE, COLOR_BLACK);
  ck_cx = u8g2_GetDisplayWidth(u) / 2;
  ck_cy = u8g2_GetDisplayHeight(u) / 2;

  /* tile colors: outside R_RING_ZONE the ring, inside the hands */
  for (ty = 0; ty < rows; ty++)
    for (tx = 0; tx < cols; tx++) {
      int16_t dx = tx * 8 + 4 - ck_cx, dy = ty * 8 + 4 - ck_cy;
      int ring = (int32_t)dx * dx + (int32_t)dy * dy >= (int32_t)R_RING_ZONE * R_RING_ZONE;
      u8g2_SetClutRegion(u, tx, ty, 1, 1, ring ? CLUT_RING : CLUT_HANDS);
    }

  ck_secs = CK_START;
  draw_clock(mem, send, ck_secs);
}

void clutclock_tick(volatile uint8_t *mem, demo_send send)
{
  ck_secs++;
  draw_clock(mem, send, ck_secs);
}

/* demo_graphics.py of the u8g2 clut branch: the GraphicsTest example, made
   for 128x64, drawn twice the size from (24, 56), with a title band and a body
   color per page */

#define GT_K  2                    /* scale of the 128x64 example geometry */
#define GT_OX 24                   /* origin of the example geometry */
#define GT_OY 56
#define GT_TITLE_Y 28              /* page title, centered */
#define CLUT_TITLE 1
#define CLUT_BODY  2
#define GT_TITLE_COLOR 0x00C0FF

#define X(x) ((int16_t)(GT_OX + GT_K * (x)))
#define Y(y) ((int16_t)(GT_OY + GT_K * (y)))

static const uint32_t gt_colors[GRAPHICS_PAGES] = {
  0xFFFFFF, 0xFFE000, 0x40FF40, 0xFF8040, 0xFF60FF, 0x60A0FF,
  0xFFFFFF, 0xFFFFFF, 0xFFE000, 0x40FF40, 0xFF8040
};

/* bitmaps, xbm */
#define CROSS_W 24
#define CROSS_H 24
static const uint8_t cross_bits[] = {
  0x00, 0x18, 0x00, 0x00, 0x24, 0x00, 0x00, 0x24, 0x00, 0x00, 0x42, 0x00,
  0x00, 0x42, 0x00, 0x00, 0x42, 0x00, 0x00, 0x81, 0x00, 0x00, 0x81, 0x00,
  0xC0, 0x00, 0x03, 0x38, 0x3C, 0x1C, 0x06, 0x42, 0x60, 0x01, 0x42, 0x80,
  0x01, 0x42, 0x80, 0x06, 0x42, 0x60, 0x38, 0x3C, 0x1C, 0xC0, 0x00, 0x03,
  0x00, 0x81, 0x00, 0x00, 0x81, 0x00, 0x00, 0x42, 0x00, 0x00, 0x42, 0x00,
  0x00, 0x42, 0x00, 0x00, 0x24, 0x00, 0x00, 0x24, 0x00, 0x00, 0x18, 0x00,
};
#define CROSS_BLOCK_W 14
#define CROSS_BLOCK_H 14
static const uint8_t cross_block_bits[] = {
  0xFF, 0x3F, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20,
  0xC1, 0x20, 0xC1, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20,
  0x01, 0x20, 0xFF, 0x3F,
};

static void gt_title(const char *text)
{
  u8g2_DrawStr(&u8g2, (u8g2_GetDisplayWidth(&u8g2) - u8g2_GetStrWidth(&u8g2, text)) / 2, GT_TITLE_Y, text);
}

static void gt_box_frame(uint8_t a)
{
  gt_title("draw_box, draw_frame");
  u8g2_DrawBox(&u8g2, X(5), Y(10), GT_K * 20, GT_K * 10);
  u8g2_DrawBox(&u8g2, X(10 + a), Y(15), GT_K * 30, GT_K * 7);
  u8g2_DrawFrame(&u8g2, X(5), Y(10 + 30), GT_K * 20, GT_K * 10);
  u8g2_DrawFrame(&u8g2, X(10 + a), Y(15 + 30), GT_K * 30, GT_K * 7);
}

static void gt_disc_circle(uint8_t a)
{
  gt_title("draw_disc, draw_circle");
  u8g2_DrawDisc(&u8g2, X(10), Y(18), GT_K * 9, U8G2_DRAW_ALL);
  u8g2_DrawDisc(&u8g2, X(24 + a), Y(16), GT_K * 7, U8G2_DRAW_ALL);
  u8g2_DrawCircle(&u8g2, X(10), Y(18 + 30), GT_K * 9, U8G2_DRAW_ALL);
  u8g2_DrawCircle(&u8g2, X(24 + a), Y(16 + 30), GT_K * 7, U8G2_DRAW_ALL);
}

static void gt_r_frame(uint8_t a)
{
  gt_title("draw_rframe, draw_rbox");
  u8g2_DrawRFrame(&u8g2, X(5), Y(10), GT_K * 40, GT_K * 30, GT_K * (a + 1));
  u8g2_DrawRBox(&u8g2, X(50), Y(10), GT_K * 25, GT_K * 40, GT_K * (a + 1));
}

static void gt_string(uint8_t a)
{
  gt_title("font direction");
  u8g2_SetFontDirection(&u8g2, 0);
  u8g2_DrawStr(&u8g2, X(30 + a), Y(31), " 0");
  u8g2_SetFontDirection(&u8g2, 1);
  u8g2_DrawStr(&u8g2, X(30), Y(31 + a), " 90");
  u8g2_SetFontDirection(&u8g2, 2);
  u8g2_DrawStr(&u8g2, X(30 - a), Y(31), " 180");
  u8g2_SetFontDirection(&u8g2, 3);
  u8g2_DrawStr(&u8g2, X(30), Y(31 - a), " 270");
  u8g2_SetFontDirection(&u8g2, 0);
}

static void gt_line(uint8_t a)
{
  gt_title("draw_line");
  u8g2_DrawLine(&u8g2, X(7 + a), Y(10), X(40), Y(55));
  u8g2_DrawLine(&u8g2, X(7 + a * 2), Y(10), X(60), Y(55));
  u8g2_DrawLine(&u8g2, X(7 + a * 3), Y(10), X(80), Y(55));
  u8g2_DrawLine(&u8g2, X(7 + a * 4), Y(10), X(100), Y(55));
}

static void gt_triangle(uint8_t a)
{
  int16_t o = a;

  gt_title("draw_triangle");
  u8g2_DrawTriangle(&u8g2, X(14), Y(7), X(45), Y(30), X(10), Y(40));
  u8g2_DrawTriangle(&u8g2, X(14 + o), Y(7 - o), X(45 + o), Y(30 - o), X(57 + o), Y(10 - o));
  u8g2_DrawTriangle(&u8g2, X(57 + o * 2), Y(10), X(45 + o * 2), Y(30), X(86 + o * 2), Y(53));
  u8g2_DrawTriangle(&u8g2, X(10 + o), Y(40 + o), X(45 + o), Y(30 + o), X(86 + o), Y(53 + o));
}

static void gt_ascii(uint8_t first, const char *text)
{
  uint8_t x, y;

  gt_title(text);
  u8g2_SetFont(&u8g2, u8g2_font_profont17_tf);
  for (y = 0; y < 6; y++)
    for (x = 0; x < 16; x++)
      u8g2_DrawGlyph(&u8g2, 24 + x * 12, 54 + y * 22, y * 16 + x + first);
  u8g2_SetFont(&u8g2, u8g2_font_profont15_tf);
}

static void gt_bitmap_overlay(uint8_t a)
{
  const uint8_t frame_size = 28;

  gt_title("bitmap overlay");
  u8g2_DrawStr(&u8g2, X(0), Y(frame_size + 12), "Solid / transparent");
  u8g2_SetBitmapMode(&u8g2, 0);              /* solid */
  u8g2_DrawFrame(&u8g2, X(0), Y(10), GT_K * frame_size, GT_K * frame_size);
  u8g2_DrawXBM(&u8g2, X(2), Y(12), CROSS_W, CROSS_H, cross_bits);
  if (a & 4)
    u8g2_DrawXBM(&u8g2, X(7), Y(17), CROSS_BLOCK_W, CROSS_BLOCK_H, cross_block_bits);
  u8g2_SetBitmapMode(&u8g2, 1);              /* transparent */
  u8g2_DrawFrame(&u8g2, X(frame_size + 5), Y(10), GT_K * frame_size, GT_K * frame_size);
  u8g2_DrawXBM(&u8g2, X(frame_size + 7), Y(12), CROSS_W, CROSS_H, cross_bits);
  if (a & 4)
    u8g2_DrawXBM(&u8g2, X(frame_size + 12), Y(17), CROSS_BLOCK_W, CROSS_BLOCK_H, cross_block_bits);
  u8g2_SetBitmapMode(&u8g2, 0);
}

static void gt_bitmap_modes(uint8_t transparent)
{
  const uint8_t frame_size = 24;

  u8g2_DrawBox(&u8g2, X(0), Y(frame_size / 2), GT_K * frame_size * 4, GT_K * frame_size);
  u8g2_DrawStr(&u8g2, X(frame_size / 2), Y(50), "Black");
  u8g2_DrawStr(&u8g2, X(frame_size * 2), Y(50), "White");
  u8g2_DrawStr(&u8g2, X(frame_size * 7 / 2), Y(50), "XOR");
  u8g2_SetBitmapMode(&u8g2, transparent);
  gt_title(transparent ? "transparent bitmap" : "solid bitmap");
  u8g2_SetDrawColor(&u8g2, 0);               /* black */
  u8g2_DrawXBM(&u8g2, X(frame_size / 2), Y(24), CROSS_W, CROSS_H, cross_bits);
  u8g2_SetDrawColor(&u8g2, 1);               /* white */
  u8g2_DrawXBM(&u8g2, X(frame_size * 2), Y(24), CROSS_W, CROSS_H, cross_bits);
  u8g2_SetDrawColor(&u8g2, 2);               /* xor */
  u8g2_DrawXBM(&u8g2, X(frame_size * 7 / 2), Y(24), CROSS_W, CROSS_H, cross_bits);
  u8g2_SetDrawColor(&u8g2, 1);
  u8g2_SetBitmapMode(&u8g2, 0);
}

/* steps of a page: 8 for animated pages, 1 for static ones */
static const uint8_t gt_steps[GRAPHICS_PAGES] = { 8, 8, 8, 8, 8, 8, 1, 1, 1, 1, 8 };

static uint8_t gt_page, gt_a;

void graphics_init(volatile uint8_t *mem)
{
  u8g2_t *u = u8g2_fbmem(mem, PAGE_ROWS);
  uint8_t cols = u8x8_GetCols(u8g2_GetU8x8(u)), rows = u8x8_GetRows(u8g2_GetU8x8(u));

  /* title band, rest of the screen: body */
  u8g2_SetClutRegion(u, 0, 0, cols, rows, CLUT_BODY);
  u8g2_SetClutRegion(u, 0, GT_TITLE_Y / 8, cols, 3, CLUT_TITLE);
  u8g2_SetClutColor(u, CLUT_TITLE, GT_TITLE_COLOR, COLOR_BLACK);
  gt_page = 0;
  gt_a = 0;
}

/* frame a of page page, drawn and sent. picture loop: in the page buffer
   modes the page is drawn once per buffer page */
void graphics_page(volatile uint8_t *mem, demo_send send, uint8_t page, uint8_t a)
{
  u8g2_t *u = &u8g2;

  u8g2_SetClutColor(u, CLUT_BODY, gt_colors[page], COLOR_BLACK);
  drv_mem = mem;
  drv_send = send;
  u8g2_FirstPage(u);
  do {
    u8g2_SetFont(u, u8g2_font_profont15_tf);
    u8g2_SetFontRefHeightExtendedText(u);
    u8g2_SetDrawColor(u, 1);
    u8g2_SetFontPosTop(u);
    u8g2_SetFontDirection(u, 0);
    switch (page) {
    case 0:  gt_box_frame(a); break;
    case 1:  gt_disc_circle(a); break;
    case 2:  gt_r_frame(a); break;
    case 3:  gt_string(a); break;
    case 4:  gt_line(a); break;
    case 5:  gt_triangle(a); break;
    case 6:  gt_ascii(32, "ASCII page 1"); break;
    case 7:  gt_ascii(160, "ASCII page 2"); break;
    case 8:  gt_bitmap_modes(0); break;
    case 9:  gt_bitmap_modes(1); break;
    default: gt_bitmap_overlay(a); break;
    }
  } while (u8g2_NextPage(u));
}

/* the next frame; the pause after it in ms: 100, or 2000 after a static page */
uint16_t graphics_step(volatile uint8_t *mem, demo_send send)
{
  uint8_t page = gt_page, steps = gt_steps[gt_page];

  graphics_page(mem, send, page, gt_a);
  if (++gt_a >= steps) {
    gt_a = 0;
    gt_page = (uint8_t)((gt_page + 1) % GRAPHICS_PAGES);
  }
  return steps == 1 ? 2000 : 100;
}

/* demo_mui.py of the u8g2 clut branch: a menu with a number, a bar and a
   list, and a page that shows the values; port of MUIInput3BtnBounce2 with
   the "mse" widgets: increment and decrement change a value directly.
   mui is built with MUI_U8G2_HIRES: on this 280 pixel high display the y
   bytes of the fields are pixels / 2, x bytes are pixels */

#define FORM_MENU  1
#define FORM_ENTER 10
#define FORM_SHOW  12
#define CLUT_MUI_BODY  0
#define CLUT_MUI_TITLE 1

static mui_t mui;
static uint8_t mu_num, mu_bar;
static uint16_t mu_animal;

static const char *const mu_animals[] = {
  "Bird", "Bison", "Cat", "Crow", "Dog", "Elephant", "Fish", "Gnu", "Horse",
  "Koala", "Lion", "Mouse", "Owl", "Rabbit", "Spider", "Turtle", "Zebra"
};

static const char *mu_animal_name(void *data, uint16_t i)
{
  (void)data;
  return mu_animals[i];
}

static uint16_t mu_animal_cnt(void *data)
{
  (void)data;
  return sizeof mu_animals / sizeof mu_animals[0];
}

static muif_t mu_list[] = {
  MUIF_U8G2_FONT_STYLE(0, u8g2_font_profont17_tf),   /* regular font */
  MUIF_U8G2_FONT_STYLE(1, u8g2_font_profont22_tf),   /* title font */
  MUIF_RO("HR", mui_hline),
  MUIF_U8G2_LABEL(),
  MUIF_RO("GP", mui_u8g2_goto_data),
  MUIF_BUTTON("GC", mui_u8g2_goto_form_w1_pi),
  MUIF_U8G2_U8_MIN_MAX("NV", &mu_num, 0, 99, mui_u8g2_u8_min_max_wm_mse_pi),
  MUIF_U8G2_U8_MIN_MAX_STEP("NB", &mu_bar, 0, 16, 1, MUI_MMS_4X_BAR, mui_u8g2_u8_bar_wm_mse_pf),
  MUIF_U8G2_U16_LIST("NA", &mu_animal, NULL, mu_animal_name, mu_animal_cnt, mui_u8g2_u16_list_line_wa_mse_pi),
  MUIF_BUTTON("GO", mui_u8g2_btn_goto_wm_fi),
};

/* demo_mui.py positions; y / 2 */
static fds_t mu_fds[] =
  MUI_FORM(1)
  MUI_STYLE(1) MUI_LABEL(50, 25, "MUI Demo") MUI_STYLE(0)
  MUI_XY("HR", 0, 31)
  /* the text of the data field: form number, then the button text, '|' separated */
  MUI_DATA("GP", MUI_10 "Enter Data|" MUI_12 "Show Data")
  MUI_XYA("GC", 70, 55, 0)
  MUI_XYA("GC", 70, 72, 1)

  MUI_FORM(10)
  MUI_STYLE(1) MUI_LABEL(40, 25, "Enter Data") MUI_STYLE(0)
  MUI_XY("HR", 0, 31)
  MUI_LABEL(40, 50, "Num:")
  MUI_LABEL(40, 67, "Bar:")
  MUI_LABEL(30, 85, "Animal:")
  MUI_XY("NV", 110, 50)
  MUI_XY("NB", 110, 67)
  MUI_XYA("NA", 110, 85, 90)
  MUI_XYAT("GO", 100, 105, 1, " Ok ")

  MUI_FORM(12)
  MUI_STYLE(1) MUI_LABEL(40, 25, "Show Data") MUI_STYLE(0)
  MUI_XY("HR", 0, 31)
  MUI_XYAT("GO", 100, 105, 1, " Ok ")
  ;

static void mu_dec(char *s, unsigned v)
{
  char t[6];
  int n = 0;

  do
    t[n++] = (char)('0' + v % 10);
  while ((v /= 10) != 0);
  while (n)
    *s++ = t[--n];
  *s = 0;
}

static void mu_show(volatile uint8_t *mem, demo_send send)
{
  u8g2_t *u = &u8g2;
  char s[6];

  u8g2_ClearBuffer(u);
  mui_Draw(&mui);
  if (mui_GetCurrentFormId(&mui) == FORM_SHOW) {
    /* the values, below the title */
    u8g2_SetFont(u, u8g2_font_profont17_tf);
    u8g2_DrawStr(u, 50, 100, "Num:");
    mu_dec(s, mu_num);
    u8g2_DrawStr(u, 120, 100, s);
    u8g2_DrawStr(u, 50, 125, "Bar:");
    mu_dec(s, mu_bar);
    u8g2_DrawStr(u, 120, 125, s);
    u8g2_DrawStr(u, 50, 150, "Animal:");
    u8g2_DrawStr(u, 120, 150, mu_animals[mu_animal]);
  }
  u8g2_clut(mem);
  send_area(send, 0, 0, u8x8_GetCols(u8g2_GetU8x8(u)), u8x8_GetRows(u8g2_GetU8x8(u)), "frame");
}

void mui_demo_init(volatile uint8_t *mem, demo_send send)
{
  u8g2_t *u = u8g2_fbmem(mem, ROWS);
  uint8_t cols = u8x8_GetCols(u8g2_GetU8x8(u)), rows = u8x8_GetRows(u8g2_GetU8x8(u));

  u8g2_SetFontPosBaseline(u);
  u8g2_SetClutColor(u, CLUT_MUI_BODY, COLOR_WHITE, COLOR_BLACK);
  u8g2_SetClutColor(u, CLUT_MUI_TITLE, GT_TITLE_COLOR, COLOR_BLACK);
  u8g2_SetClutRegion(u, 0, 0, cols, rows, CLUT_MUI_BODY);
  u8g2_SetClutRegion(u, 0, 3, cols, 4, CLUT_MUI_TITLE);   /* title band, y 24..55 */
  mui_Init(&mui, u, mu_fds, mu_list, sizeof mu_list / sizeof mu_list[0]);
  mui_GotoForm(&mui, FORM_MENU, 0);
  mu_show(mem, send);
}

/* w s: previous, next field. a d: decrease, increase a value. space, enter:
   select. other keys are ignored */
void mui_demo_key(volatile uint8_t *mem, demo_send send, char key)
{
  switch (key) {
  case 'w':  mui_PrevField(&mui); break;
  case 's':  mui_NextField(&mui); break;
  case 'a':  mui_SendValueDecrement(&mui); break;
  case 'd':  mui_SendValueIncrement(&mui); break;
  case ' ':
  case '\r':
  case '\n': mui_SendSelect(&mui); break;
  default:   return;
  }
  mu_show(mem, send);
}

/* ------------------------------------------------------------------ */

/* whole screens in the three formats, then rectangles with the swapped clut */
const struct step pattern_steps[] = {
  { STEP_REQ,    FMT_RGB565, 0, 0, LCD_W, LCD_H, CLUT_OFS, "dma rgb565 screen" },
  { STEP_DELAY,  0, 0, 0, 2000, 0, 0, 0 },
  { STEP_COLMOD, 0x03, 0, 0, 0, 0, 0, 0 },
  { STEP_REQ,    FMT_RGB444, 0, 0, LCD_W, LCD_H, CLUT4_OFS, "dma rgb444 screen" },
  { STEP_DELAY,  0, 0, 0, 2000, 0, 0, 0 },
  { STEP_COLMOD, 0x06, 0, 0, 0, 0, 0, 0 },
  { STEP_REQ,    FMT_RGB666, 0, 0, LCD_W, LCD_H, CLUT_OFS, "dma rgb666 screen" },
  { STEP_DELAY,  0, 0, 0, 2000, 0, 0, 0 },
  { STEP_COLMOD, 0x05, 0, 0, 0, 0, 0, 0 },
  { STEP_REQ,    FMT_RGB565, 0, 0, LCD_W, LCD_H, CLUT_OFS, "dma rgb565 screen" },
  { STEP_DELAY,  0, 0, 0, 1000, 0, 0, 0 },
  { STEP_REQ,    FMT_RGB565, 5, 10, 101, 50, CLUT2_OFS, "dma 101x50 at tile 5,10" },
  { STEP_REQ,    FMT_RGB565, 20, 1, 8, 200, CLUT2_OFS, "dma 8x200 at tile 20,1" },
  { STEP_REQ,    FMT_RGB565, 3, 30, 5, 3, CLUT2_OFS, "dma 5x3 at tile 3,30" },
  { STEP_END,    0, 0, 0, 0, 0, 0, 0 }
};

const struct step palette_steps[] = {
  { STEP_COLMOD, 0x03, 0, 0, 0, 0, 0, 0 },
  { STEP_REQ,    FMT_RGB444, 0, 0, LCD_W, LCD_H, CLUT_OFS, "dma rgb444 screen" },
  { STEP_END,    0, 0, 0, 0, 0, 0, 0 }
};

const struct step border_steps[] = {
  { STEP_REQ,    FMT_RGB565, 0, 0, LCD_W, LCD_H, CLUT3_OFS, "dma rgb565 screen" },
  { STEP_END,    0, 0, 0, 0, 0, 0, 0 }
};
