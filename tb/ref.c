/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  reference for clut_dma.
  the bitmap is in horizontal layout: a byte is 8 pixels of a pixel row,
  leftmost pixel in bit 7. for each request, the same pixels are put in the
  vertical tile layout of u8g2, and u8x8_clut_stream_rgb565() or
  u8x8_clut_stream_rgb444() of u8g2 (csrc/u8x8_clut.c) gives the expected
  bytes. pixels and rows outside the request (W, H not a multiple of 8) are
  dropped. rgb666 is the rgb565 stream of u8g2, converted here. writes:
    mem.hex       memory image, 32-bit little-endian words, for $readmemh
    tests.txt     one request per line: format tile_cols first w h fb_start map clut
    expected.txt  the bytes u8g2 sends to the display, per request
    reads.txt     the memory reads of clut_dma per request, one per line:
                  a map word at the first tile of a pixel row and at a multiple
                  of 8 tiles, a bitmap word at the first tile of a pixel row
                  and at a word boundary, a clut word when the index changes
  "measure": only whole screens, for the spi underrun measurement.
  "narrow": a rectangle 1 tile wide on a 320x240 display, for the spi
  underrun measurement.
  "uniform": as "measure", with one clut index for all tiles.
  "vmeasure": as "measure", the bitmap in vertical layout.
  vertical layout (clut_dma CTRL bit 4): the bitmap is the tile layout of
  u8g2, tile t is 8 bytes at fb + 8*t, byte c the pixel column c, bit r the
  pixel row r; fb 8-byte aligned. FB_START is the address of the first tile. the memory is given to
  u8x8_clut_stream_rgb565/444() as it is, without conversion. in tests.txt
  and expected.txt, bit 2 of the format is the vertical layout, bit 3 NO_MAP
  (clut_dma CTRL bit 5): no map, every tile clut entry 0, as u8g2 without a
  tile_clut_map; no map reads, one clut read per request. memory
  reads: a map word and a clut word as for the horizontal layout, and 2
  bitmap words per tile and pixel row.
  "fabric": for fpga/tb/tb_fabric.v, a 240x280 display in the 16 kbyte memory
  of clut_fabric. tests.txt: format tile_cols first w h fb_start. expected.txt:
  the spi bytes with d/c, "c 2a" a command, "d 00" data: the display window
  commands and the pixels.
  "bpp2": the tests above, then requests in 2 bits per pixel (clut_dma
  parameter BPP2, CTRL bit 6; bit 4 of the format in tests.txt), horizontal
  layout. u8g2 has no 2 bpp; the expected bytes come from the model in
  request2(): a byte is 4 pixels, the leftmost in bits 7:6; a pixel is a
  code c of 2 bits; clut entry of 64 bits at clut + 8 * idx, c selects
  word c >> 1, bits 15:0 when c is odd, bits 31:16 when even (word 0 as
  the 1-bit clut word). What the 4 colors mean is up to the software that
  fills the clut. two checks of the model itself: a 2 bpp bitmap with the
  high bits 0 gives the bytes of u8g2 for the same 1-bit bitmap and word 0
  of the clut, and the byte 0x1b (codes 0 1 2 3) gives the four colors in
  code order (variant 15 of the publication).
  memory reads: 2 clut words when the index changes, a bitmap word holds 2
  tiles of a pixel row.
  "lvgl": as "bpp2" without the vertical layout, for clut_dma with VERT = 0.
  "bmeasure": as "measure", in 2 bits per pixel: whole 240x240 screens, a
  random bitmap of 14,400 bytes at FB_D, MAP_A, CLUT_D.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "u8x8_clut_driver.h"

#define MEM_SIZE 65536

/* formats */
#define F_RGB565 0
#define F_RGB444 1
#define F_RGB666 2

/* memory layout */
#define CLUT_A 0x0000   /* random */
#define CLUT_B 0x0040   /* vga palette */
#define MAP_A  0x0100   /* random */
#define MAP_B  0x0400   /* all index 5 */
#define MAP_C  0x0800   /* alternating index 0, 15 */
#define FB_A   0x1003   /* random. not aligned */
#define FB_B   0x4000   /* all pixels clear */
#define FB_C   0x6800   /* all pixels set */
#define MAX_TILES 1200  /* 320x240 */

/* 2 bpp ("bpp2"), filled only in that mode */
#define CLUT_D 0x0080   /* random, 128 bytes */
#define CLUT_E 0x0C00   /* word 0 of each entry the 1-bit word of CLUT_A, word 1 random */
#define CLUT_F 0x0C80   /* every entry fg 0xaaaa, bg 0xbbbb, near fg 0xcccc, near bg 0xdddd */
#define FB_F   0x0D00   /* 480 bytes 0x1b: pixels 00 01 10 11 */
#define FB_D   0x9000   /* random, 16 * MAX_TILES2 bytes */
#define FB_E   0xB800   /* FB_A, 30 tiles wide and 240 rows, in 2 bpp with the high bits 0 */
#define MAX_TILES2 600

/* fabric: 240x280, 30x35 tiles, in 16 kbyte */
#define FAB_SIZE   0x4000
#define FAB_FB     0x0000   /* 8400 bytes */
#define FAB_MAP    0x2100   /* 525 bytes */
#define FAB_CLUT   0x2340
#define FAB_COLS   30
#define FAB_ROWS   35
#define FAB_YOFS   20       /* st7789 240x280: glass starts at ram row 20 */

static uint8_t mem[MEM_SIZE];
static FILE *tests;
static FILE *expected;
static FILE *reads;
static uint32_t rnd_state = 1;
static int fabric;      /* spi bytes with d/c */
static int no_map;      /* requests with CTRL NO_MAP */
static int quiet;       /* no tests.txt, reads.txt, expected.txt header */
static int tnum;        /* test number */

/* the bytes of put_data into a buffer instead of expected.txt */
static uint8_t *capout;
static int ncapout;
static uint8_t cap_a[240 * 240 * 3], cap_b[240 * 240 * 3];

/* output of u8x8_clut_stream_rgb565/444() for one run of tiles */
static uint8_t cap[8 * 40 * 8 * 2];
static int ncap;

/* xorshift32 */
static uint32_t rnd(void)
{
  rnd_state ^= rnd_state << 13;
  rnd_state ^= rnd_state >> 17;
  rnd_state ^= rnd_state << 5;
  return rnd_state;
}

static void put_data(uint8_t b)
{
  if ( capout != NULL )
    capout[ncapout++] = b;
  else
    fprintf(expected, fabric ? "d %02x\n" : "%02x\n", b);
}

static void put_cmd(uint8_t b)
{
  fprintf(expected, "c %02x\n", b);
}

/* rgb565 to rgb666: 8 bits per color, widened by replicating the most significant bits */
static void put_rgb666(uint16_t c)
{
  uint8_t r5 = (uint8_t)(c >> 11);
  uint8_t g6 = (uint8_t)((c >> 5) & 0x3f);
  uint8_t b5 = (uint8_t)(c & 0x1f);

  put_data((uint8_t)((r5 << 3) | (r5 >> 2)));
  put_data((uint8_t)((g6 << 2) | (g6 >> 4)));
  put_data((uint8_t)((b5 << 3) | (b5 >> 2)));
}

/* output of u8x8_clut_stream_rgb565() and u8x8_clut_stream_rgb444() */
uint8_t u8x8_cad_SendData(u8x8_t *u8x8, uint8_t cnt, uint8_t *data)
{
  (void)u8x8;
  memcpy(cap + ncap, data, cnt);
  ncap += cnt;
  return 1;
}

static uint16_t rd16(uint32_t addr)
{
  return (uint16_t)(mem[addr] | (mem[addr + 1] << 8));
}

static void wr16(uint32_t addr, uint16_t v)
{
  mem[addr] = (uint8_t)v;
  mem[addr + 1] = (uint8_t)(v >> 8);
}

/*
  rectangle of w by h pixels, top left at tile (tx, ty). tile_cols tiles per
  tile row, and bitmap bytes per pixel row. per tile row of the rectangle:
  the tiles in u8g2 vertical layout, one u8x8 draw tile run, then only the
  pixels and rows inside the rectangle.
*/
static void request(int vert, int fmt, int tile_cols, int tx, int ty, int w, int h, uint32_t fb, uint32_t map, uint32_t clut)
{
  static clut_t tab;
  u8x8_t u8x8;
  uint8_t vt[8 * 40];
  int first = ty * tile_cols + tx;
  uint32_t fb_start = vert ? fb + 8 * first : fb + 8 * ty * tile_cols + tx;   /* the first tile */
  int fmt_v = fmt | (vert << 2) | (no_map << 3);
  int n = (w + 7) / 8;
  int i, c, r, k, p;

  if ( fabric )
  {
    /* display window, as tb_fabric.v sends it: CASET, RASET, RAMWR */
    int x0 = 8 * tx;
    int x1 = x0 + w - 1;
    int y0 = 8 * ty + FAB_YOFS;
    int y1 = y0 + h - 1;

    fprintf(tests, "%x %x %x %x %x %x\n", fmt, tile_cols, first, w, h, fb_start);
    put_cmd(0x2a);
    put_data((uint8_t)(x0 >> 8)); put_data((uint8_t)x0);
    put_data((uint8_t)(x1 >> 8)); put_data((uint8_t)x1);
    put_cmd(0x2b);
    put_data((uint8_t)(y0 >> 8)); put_data((uint8_t)y0);
    put_data((uint8_t)(y1 >> 8)); put_data((uint8_t)y1);
    put_cmd(0x2c);
  }
  else if ( !quiet )
  {
    fprintf(tests, "%x %x %x %x %x %x %x %x\n", fmt_v, tile_cols, first, w, h, fb_start, map, clut);
    fprintf(expected, "# %d %d %d %d %d %d\n", tnum++, fmt_v, tile_cols, first, w, h);
  }

  memset(&u8x8, 0, sizeof(u8x8));
  for ( i = 0; i < U8X8_CLUT_ENTRY_CNT; i++ )
  {
    tab[i][0] = rd16(clut + 4 * i);
    tab[i][1] = rd16(clut + 4 * i + 2);
  }
  u8x8.clut = tab;
  u8x8.tile_clut_map = no_map ? NULL : mem + map;

  /* memory reads of clut_dma, with the clut index kept over the request */
  {
    int nreads = 0, valid = 0, last_idx = 0, y;

    for ( y = 0; y < h && w > 0; y++ )
      for ( c = 0; c < n; c++ )
      {
        int tile = first + (y / 8) * tile_cols + c;
        uint32_t fbaddr = fb_start + y * tile_cols + c;
        int idx = no_map ? 0 : (mem[map + tile / 2] >> (4 * (tile & 1))) & 15;

        if ( !no_map && (c == 0 || tile % 8 == 0) )
          nreads++;
        if ( !valid || idx != last_idx )
          nreads++;
        valid = 1;
        last_idx = idx;
        if ( vert )
          nreads += 2;
        else if ( c == 0 || fbaddr % 4 == 0 )
          nreads++;
      }
    if ( !quiet )
      fprintf(reads, "%d\n", nreads);
  }

  if ( w == 0 || h == 0 )
    return;

  for ( k = 0; k * 8 < h; k++ )
  {
    /* the tiles of this tile row in u8g2 vertical layout: in the memory as
       it is, or converted from the horizontal layout */
    uint8_t *tp = vt;

    if ( vert )
      tp = mem + fb + 8 * (first + k * tile_cols);
    else
    {
      memset(vt, 0, sizeof(vt));
      for ( i = 0; i < n; i++ )
        for ( c = 0; c < 8; c++ )
          for ( r = 0; r < 8; r++ )
          {
            int y = 8 * (ty + k) + r;
            uint8_t b = mem[fb + y * tile_cols + tx + i];
            if ( b & (0x80 >> c) )
              vt[8 * i + c] |= (uint8_t)(1 << r);
          }
    }

    ncap = 0;
    if ( fmt == F_RGB444 )
      u8x8_clut_stream_rgb444(&u8x8, tp, (uint8_t)n, (uint16_t)(first + k * tile_cols));
    else
      u8x8_clut_stream_rgb565(&u8x8, tp, (uint8_t)n, (uint16_t)(first + k * tile_cols));

    /* pixel rows of the tile row, w pixels each */
    for ( r = 0; r < 8 && k * 8 + r < h; r++ )
    {
      if ( fmt == F_RGB444 )
      {
        uint8_t *row = cap + r * 12 * n;
        for ( p = 0; p < w / 2 * 3; p++ )
          put_data(row[p]);
      }
      else
      {
        uint8_t *row = cap + r * 16 * n;
        for ( p = 0; p < w; p++ )
        {
          if ( fmt == F_RGB666 )
            put_rgb666((uint16_t)((row[2 * p] << 8) | row[2 * p + 1]));
          else
          {
            put_data(row[2 * p]);
            put_data(row[2 * p + 1]);
          }
        }
      }
    }
  }
}

/* random rectangle on a random display. rgb444: even width */
static void random_request(int vert, int fmt)
{
  int tile_cols = 1 + rnd() % 40;
  int tile_rows = MAX_TILES / tile_cols;
  int tx, ty, w, h;

  /* SIZE.H has 12 bits: at most 4095 pixel rows */
  if ( tile_rows > 511 )
    tile_rows = 511;
  tx = rnd() % tile_cols;
  ty = rnd() % tile_rows;
  w = 1 + rnd() % ((tile_cols - tx) * 8);
  h = 1 + rnd() % ((tile_rows - ty) * 8);

  if ( fmt == F_RGB444 && (w & 1) )
    w++;
  request(vert, fmt, tile_cols, tx, ty, w, h, vert ? FB_A + 5 : FB_A, MAP_A, CLUT_A);
}

/*
  2 bpp, horizontal layout: rectangle of w by h pixels, top left at tile (tx,
  ty); tile_cols tiles per tile row, 2 * tile_cols bitmap bytes per pixel row
*/
static uint16_t color2(uint32_t clut, int idx, int code)
{
  uint32_t e = clut + 8 * (uint32_t)idx;

  switch ( code )
  {
    case 0:  return rd16(e + 2);    /* word 0 bits 31:16 */
    case 1:  return rd16(e);        /* word 0 bits 15:0 */
    case 2:  return rd16(e + 6);    /* word 1 bits 31:16 */
    default: return rd16(e + 4);    /* word 1 bits 15:0 */
  }
}

static void request2(int fmt, int tile_cols, int tx, int ty, int w, int h, uint32_t fb, uint32_t map, uint32_t clut)
{
  int first = ty * tile_cols + tx;
  int stride = 2 * tile_cols;
  uint32_t fb_start = fb + 8 * ty * stride + 2 * tx;   /* the first tile */
  int fmt_v = fmt | (no_map << 3) | 16;
  int n = (w + 7) / 8;
  int x, y, c;

  if ( !quiet )
  {
    fprintf(tests, "%x %x %x %x %x %x %x %x\n", fmt_v, tile_cols, first, w, h, fb_start, map, clut);
    fprintf(expected, "# %d %d %d %d %d %d\n", tnum++, fmt_v, tile_cols, first, w, h);
  }

  /* memory reads of clut_dma: a map word as in 1 bpp, 2 clut words when the
     index changes, a bitmap word at the first tile of a pixel row and at a
     word boundary */
  {
    int nreads = 0, valid = 0, last_idx = 0;

    for ( y = 0; y < h && w > 0; y++ )
      for ( c = 0; c < n; c++ )
      {
        int tile = first + (y / 8) * tile_cols + c;
        uint32_t fbaddr = fb_start + y * stride + 2 * c;
        int idx = no_map ? 0 : (mem[map + tile / 2] >> (4 * (tile & 1))) & 15;

        if ( !no_map && (c == 0 || tile % 8 == 0) )
          nreads++;
        if ( !valid || idx != last_idx )
          nreads += 2;
        valid = 1;
        last_idx = idx;
        if ( c == 0 || fbaddr % 4 == 0 )
          nreads++;
      }
    if ( !quiet )
      fprintf(reads, "%d\n", nreads);
  }

  for ( y = 0; y < h && w > 0; y++ )
    for ( x = 0; x < w; x++ )
    {
      int tile = first + (y / 8) * tile_cols + x / 8;
      int idx = no_map ? 0 : (mem[map + tile / 2] >> (4 * (tile & 1))) & 15;
      int code = (mem[fb_start + y * stride + x / 4] >> (6 - 2 * (x % 4))) & 3;
      uint16_t p = color2(clut, idx, code);

      if ( fmt == F_RGB444 )
      {
        /* pixel pairs: 3 bytes, {p0[11:0], p1[11:0]} */
        if ( x % 2 == 0 )
          continue;
        {
          int code0 = (mem[fb_start + y * stride + (x - 1) / 4] >> (6 - 2 * ((x - 1) % 4))) & 3;
          uint16_t p0 = color2(clut, idx, code0);   /* same tile: x odd */

          put_data((uint8_t)(p0 >> 4));
          put_data((uint8_t)(((p0 & 0xf) << 4) | ((p >> 8) & 0xf)));
          put_data((uint8_t)p);
        }
      }
      else if ( fmt == F_RGB666 )
        put_rgb666(p);
      else
      {
        put_data((uint8_t)(p >> 8));
        put_data((uint8_t)p);
      }
    }
}

/* random 2 bpp rectangle on a random display. rgb444: even width */
static void random_request2(int fmt)
{
  int tile_cols = 1 + rnd() % 40;
  int tile_rows = MAX_TILES2 / tile_cols;
  int tx, ty, w, h;

  if ( tile_rows > 511 )
    tile_rows = 511;
  tx = rnd() % tile_cols;
  ty = rnd() % tile_rows;
  w = 1 + rnd() % ((tile_cols - tx) * 8);
  h = 1 + rnd() % ((tile_rows - ty) * 8);

  if ( fmt == F_RGB444 && (w & 1) )
    w++;
  request2(fmt, tile_cols, tx, ty, w, h, FB_D, MAP_A, CLUT_D);
}

/* the 2 bpp regions, after fill_memory */
static void fill_memory2(void)
{
  int i, x, y;

  for ( i = 0; i < 128; i += 2 )
    wr16(CLUT_D + i, (uint16_t)rnd());
  for ( i = 0; i < 16; i++ )
  {
    wr16(CLUT_E + 8 * i, rd16(CLUT_A + 4 * i));
    wr16(CLUT_E + 8 * i + 2, rd16(CLUT_A + 4 * i + 2));
    wr16(CLUT_E + 8 * i + 4, (uint16_t)rnd());
    wr16(CLUT_E + 8 * i + 6, (uint16_t)rnd());
    wr16(CLUT_F + 8 * i, 0xaaaa);
    wr16(CLUT_F + 8 * i + 2, 0xbbbb);
    wr16(CLUT_F + 8 * i + 4, 0xcccc);
    wr16(CLUT_F + 8 * i + 6, 0xdddd);
  }
  for ( i = 0; i < 480; i++ )
    mem[FB_F + i] = 0x1b;
  for ( i = 0; i < 16 * MAX_TILES2; i++ )
    mem[FB_D + i] = (uint8_t)rnd();
  /* FB_A in 2 bpp: pixel b becomes 0b */
  for ( y = 0; y < 240; y++ )
    for ( x = 0; x < 240; x++ )
      if ( mem[FB_A + 30 * y + x / 8] & (0x80 >> (x % 8)) )
        mem[FB_E + 60 * y + x / 4] |= (uint8_t)(1 << (6 - 2 * (x % 4)));
}

/*
  check of the model: FB_E with CLUT_E in 2 bpp gives the bytes of u8g2 for
  FB_A with CLUT_A in 1 bpp. then the same request as a test of clut_dma
*/
static int check_degenerate(int fmt, int tx, int ty, int w, int h)
{
  int na;

  quiet = 1;
  capout = cap_a; ncapout = 0;
  request(0, fmt, 30, tx, ty, w, h, FB_A, MAP_A, CLUT_A);
  na = ncapout;
  capout = cap_b; ncapout = 0;
  request2(fmt, 30, tx, ty, w, h, FB_E, MAP_A, CLUT_E);
  capout = NULL;
  quiet = 0;
  if ( na != ncapout || memcmp(cap_a, cap_b, (size_t)na) != 0 )
  {
    fprintf(stderr, "ref: 2 bpp model differs from u8g2: fmt %d %d %d %d %d\n", fmt, tx, ty, w, h);
    return 0;
  }
  request2(fmt, 30, tx, ty, w, h, FB_E, MAP_A, CLUT_E);
  return 1;
}

/* check of the model: codes 0 1 2 3, the four colors in code order */
static int check_directed(void)
{
  static const uint8_t want[8] = { 0xbb, 0xbb, 0xaa, 0xaa, 0xdd, 0xdd, 0xcc, 0xcc };

  quiet = 1;
  capout = cap_a; ncapout = 0;
  request2(F_RGB565, 30, 0, 0, 4, 1, FB_F, MAP_A, CLUT_F);
  capout = NULL;
  quiet = 0;
  if ( ncapout != 8 || memcmp(cap_a, want, 8) != 0 )
  {
    fprintf(stderr, "ref: 2 bpp model: wrong colors for 0x1b\n");
    return 0;
  }
  return 1;
}

/* the 2 bpp tests */
static int tests2(void)
{
  int fmt, i;

  if ( !check_directed() )
    return 0;
  for ( fmt = F_RGB565; fmt <= F_RGB666; fmt++ )
  {
    int odd = (fmt == F_RGB444) ? 0 : 1;   /* rgb444: even widths */

    request2(fmt, 30, 0, 0, 240, 8, FB_D, MAP_A, CLUT_D);             /* one tile row */
    request2(fmt, 30, 7, 5, 88, 8, FB_D, MAP_A, CLUT_D);              /* run in the middle of a tile row */
    request2(fmt, 30, 29, 19, 8, 8, FB_D, MAP_A, CLUT_D);             /* last tile of 240x160 */
    request2(fmt, 30, 0, 0, 240, 160, FB_D, MAP_A, CLUT_D);           /* 240x160, the random bitmap */
    request2(fmt, 30, 4, 10, 72, 48, FB_D, MAP_A, CLUT_D);            /* rectangle */
    request2(fmt, 30, 29, 3, 8, 80, FB_D, MAP_A, CLUT_D);             /* one tile column, right edge */
    request2(fmt, 17, 0, 0, 134 + odd, 32, FB_D, MAP_A, CLUT_D);      /* odd width */
    request2(fmt, 30, 0, 0, 240, 135, FB_D, MAP_A, CLUT_D);           /* partial tile row */
    request2(fmt, 30, 3, 2, 6 + odd, 3, FB_D, MAP_A, CLUT_D);         /* less than a tile */
    request2(fmt, 30, 0, 0, 240, 16, FB_B, MAP_B, CLUT_D);            /* all 00: background */
    request2(fmt, 30, 0, 0, 240, 16, FB_C, MAP_C, CLUT_D);            /* all 11: near the foreground */
    request2(fmt, 30, 0, 0, 240, 8, FB_F, MAP_A, CLUT_F);             /* 00 01 10 11 */
    request2(fmt, 30, 0, 0, 0, 40, FB_D, MAP_A, CLUT_D);              /* empty: w = 0 */
    request2(fmt, 30, 0, 0, 40, 0, FB_D, MAP_A, CLUT_D);              /* empty: h = 0 */
    for ( i = 0; i < 20; i++ )
      random_request2(fmt);
    /* the high bits 0: as u8g2 in 1 bpp */
    if ( !check_degenerate(fmt, 0, 0, 240, 240) ||
         !check_degenerate(fmt, 4, 10, 72, 48) ||
         !check_degenerate(fmt, 20, 12, 80, 40) ||
         !check_degenerate(fmt, 1, 1, 36 + odd, 13) )
      return 0;
    no_map = 1;
    request2(fmt, 30, 0, 0, 240, 160, FB_D, MAP_C, CLUT_D);           /* whole bitmap */
    request2(fmt, 30, 4, 10, 72 + odd, 45, FB_D, MAP_C, CLUT_D);      /* rectangle, partial tiles */
    for ( i = 0; i < 5; i++ )
      random_request2(fmt);
    no_map = 0;
  }
  return 1;
}

static void fill_memory(void)
{
  /* vga palette as rgb565, foreground/background swapped half way */
  static const uint16_t vga[16] = {
    0x0000, 0x0015, 0x0540, 0x0555, 0xA800, 0xA815, 0xAAA0, 0xAD55,
    0x52AA, 0x52BF, 0x57EA, 0x57FF, 0xFAAA, 0xFABF, 0xFFEA, 0xFFFF };
  int i;

  for ( i = 0; i < 16; i++ )
  {
    /* random 16-bit, also in the upper bits that rgb444 ignores */
    wr16(CLUT_A + 4 * i, (uint16_t)rnd());
    wr16(CLUT_A + 4 * i + 2, (uint16_t)rnd());
    wr16(CLUT_B + 4 * i, vga[15 - i]);
    wr16(CLUT_B + 4 * i + 2, vga[i]);
  }
  for ( i = 0; i < MAX_TILES / 2; i++ )
  {
    mem[MAP_A + i] = (uint8_t)rnd();
    mem[MAP_B + i] = 0x55;
    mem[MAP_C + i] = 0xF0;
  }
  for ( i = 0; i < MAX_TILES * 8; i++ )
  {
    mem[FB_A + i] = (uint8_t)rnd();
    mem[FB_B + i] = 0x00;
    mem[FB_C + i] = 0xFF;
  }
}

static int open_files(int size)
{
  FILE *hex;
  int i;

  hex = fopen("mem.hex", "w");
  tests = fopen("tests.txt", "w");
  expected = fopen("expected.txt", "w");
  reads = fopen("reads.txt", "w");
  if ( hex == NULL || tests == NULL || expected == NULL || reads == NULL )
  {
    perror("ref");
    return 0;
  }
  for ( i = 0; i < size; i += 4 )
    fprintf(hex, "%02x%02x%02x%02x\n", mem[i + 3], mem[i + 2], mem[i + 1], mem[i]);
  fclose(hex);
  return 1;
}

/* memory of clut_fabric, 240x280 */
static int fabric_main(void)
{
  int i;

  for ( i = 0; i < FAB_COLS * FAB_ROWS * 8; i++ )
    mem[FAB_FB + i] = (uint8_t)rnd();
  for ( i = 0; i < (FAB_COLS * FAB_ROWS + 1) / 2; i++ )
    mem[FAB_MAP + i] = (uint8_t)rnd();
  for ( i = 0; i < 16 * 4; i++ )
    mem[FAB_CLUT + i] = (uint8_t)rnd();
  if ( !open_files(FAB_SIZE) )
    return 1;

  fabric = 1;
  request(0, F_RGB444, FAB_COLS, 0, 0, 240, 280, FAB_FB, FAB_MAP, FAB_CLUT);      /* whole screen */
  request(0, F_RGB565, FAB_COLS, 3, 5, 56, 32, FAB_FB, FAB_MAP, FAB_CLUT);        /* rectangle */
  request(0, F_RGB666, FAB_COLS, 25, 34, 40, 8, FAB_FB, FAB_MAP, FAB_CLUT);       /* bottom right */
  request(0, F_RGB565, FAB_COLS, 0, 0, 240, 8, FAB_FB, FAB_MAP, FAB_CLUT);        /* top tile row */
  request(0, F_RGB565, FAB_COLS, 1, 1, 37, 13, FAB_FB, FAB_MAP, FAB_CLUT);        /* odd size */

  fclose(tests);
  fclose(expected);
  fclose(reads);
  return 0;
}

/* "measure": only whole screens, for the spi underrun measurement */
int main(int argc, char **argv)
{
  int fmt, i;
  int measure = argc > 1 && strcmp(argv[1], "measure") == 0;
  int narrow = argc > 1 && strcmp(argv[1], "narrow") == 0;
  int uniform = argc > 1 && strcmp(argv[1], "uniform") == 0;
  int vmeasure = argc > 1 && strcmp(argv[1], "vmeasure") == 0;
  int lvgl = argc > 1 && strcmp(argv[1], "lvgl") == 0;
  int bpp2 = lvgl || (argc > 1 && strcmp(argv[1], "bpp2") == 0);
  int bmeasure = argc > 1 && strcmp(argv[1], "bmeasure") == 0;
  int vert;

  if ( argc > 1 && strcmp(argv[1], "fabric") == 0 )
    return fabric_main();

  fill_memory();
  if ( bpp2 || bmeasure )
    fill_memory2();
  if ( bmeasure )   /* a whole 240x240 screen in 2 bpp, over FB_E */
    for ( i = 0; i < 16 * 900; i++ )
      mem[FB_D + i] = (uint8_t)rnd();
  if ( !open_files(MEM_SIZE) )
    return 1;

  for ( vert = 0; vert <= 1; vert++ )
  for ( fmt = F_RGB565; fmt <= F_RGB666; fmt++ )
  {
    int odd = (fmt == F_RGB444) ? 0 : 1;   /* rgb444: even widths */
    /* vertical: the random bitmap 8-byte aligned, FB_A + 5 */
    uint32_t fb = vert ? FB_A + 5 : FB_A;

    if ( lvgl && vert )
      continue;
    if ( bmeasure )
    {
      if ( !vert )
        request2(fmt, 30, 0, 0, 240, 240, FB_D, MAP_A, CLUT_D);       /* whole screen, 2 bpp */
      continue;
    }
    if ( narrow || uniform || measure )
    {
      if ( vert )
        continue;
      if ( narrow )
        request(0, fmt, 40, 20, 0, 8, 240, FB_A, MAP_A, CLUT_A);    /* 1 tile wide, 320x240 */
      else
        request(0, fmt, 30, 0, 0, 240, 240, FB_A, uniform ? MAP_B : MAP_A, CLUT_A); /* whole screen */
      continue;
    }
    if ( vmeasure )
    {
      if ( vert )
        request(1, fmt, 30, 0, 0, 240, 240, fb, MAP_A, CLUT_A);     /* whole screen, vertical */
      continue;
    }
    request(vert, fmt, 30, 0, 0, 240, 8, fb, MAP_A, CLUT_A);            /* one tile row, 240 wide */
    request(vert, fmt, 30, 7, 5, 88, 8, fb, MAP_A, CLUT_A);             /* run in the middle of a tile row */
    request(vert, fmt, 30, 29, 29, 8, 8, fb, MAP_A, CLUT_A);            /* last tile of 240x240 */
    request(vert, fmt, 30, 0, 0, 240, 240, fb, MAP_A, CLUT_A);          /* whole screen, 240x240 */
    request(vert, fmt, 30, 4, 10, 72, 48, fb, MAP_A, CLUT_A);           /* rectangle */
    request(vert, fmt, 30, 29, 3, 8, 80, fb, MAP_A, CLUT_A);            /* one tile column, right edge */
    request(vert, fmt, 30, 20, 12, 80, 40, fb, MAP_A, CLUT_A);          /* rectangle at the right edge */
    request(vert, fmt, 40, 0, 0, 320, 240, fb, MAP_A, CLUT_A);          /* whole screen, 320x240 */
    request(vert, fmt, 17, 0, 0, 134 + odd, 240, fb, MAP_A, CLUT_A);    /* 135x240, odd width */
    request(vert, fmt, 30, 0, 0, 240, 135, fb, MAP_A, CLUT_A);          /* 240x135, partial tile row */
    request(vert, fmt, 30, 3, 2, 6 + odd, 3, fb, MAP_A, CLUT_A);        /* less than a tile */
    request(vert, fmt, 30, 0, 0, 240, 16, FB_B, MAP_B, CLUT_B);         /* all background, index 5 */
    request(vert, fmt, 30, 0, 0, 240, 16, FB_C, MAP_C, CLUT_B);         /* all foreground, index 0 and 15 */
    request(vert, fmt, 30, 0, 0, 0, 40, fb, MAP_A, CLUT_A);             /* empty: w = 0 */
    request(vert, fmt, 30, 0, 0, 40, 0, fb, MAP_A, CLUT_A);             /* empty: h = 0 */
    for ( i = 0; i < 20; i++ )
      random_request(vert, fmt);
    /* NO_MAP: the map is not read; MAP_C would give index 0 and 15 */
    no_map = 1;
    request(vert, fmt, 30, 0, 0, 240, 240, fb, MAP_C, CLUT_A);          /* whole screen, 240x240 */
    request(vert, fmt, 30, 4, 10, 72 + odd, 45, fb, MAP_C, CLUT_A);     /* rectangle, partial tiles */
    request(vert, fmt, 30, 0, 0, 240, 16, FB_C, MAP_C, CLUT_B);         /* all foreground */
    for ( i = 0; i < 5; i++ )
      random_request(vert, fmt);
    no_map = 0;
  }
  if ( bpp2 && !tests2() )
    return 1;

  fclose(tests);
  fclose(expected);
  fclose(reads);
  return 0;
}
