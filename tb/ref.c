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
  "fabric": for fpga/tb/tb_fabric.v, a 240x280 display in the 16 kbyte memory
  of clut_fabric. tests.txt: format tile_cols first w h fb_start. expected.txt:
  the spi bytes with d/c, "c 2a" a command, "d 00" data: the display window
  commands and the pixels.
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
  fprintf(expected, fabric ? "d %02x\n" : "%02x\n", b);
}

static void put_cmd(uint8_t b)
{
  fprintf(expected, "c %02x\n", b);
}

/* rgb565 to rgb666: 6 bits in bits 7:2, 5-bit red and blue extended by replication */
static void put_rgb666(uint16_t c)
{
  uint8_t r5 = (uint8_t)(c >> 11);
  uint8_t g6 = (uint8_t)((c >> 5) & 0x3f);
  uint8_t b5 = (uint8_t)(c & 0x1f);

  put_data((uint8_t)((r5 << 3) | ((r5 >> 4) << 2)));
  put_data((uint8_t)(g6 << 2));
  put_data((uint8_t)((b5 << 3) | ((b5 >> 4) << 2)));
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
static void request(int fmt, int tile_cols, int tx, int ty, int w, int h, uint32_t fb, uint32_t map, uint32_t clut)
{
  static int t = 0;
  u8x8_t u8x8;
  uint8_t vt[8 * 40];
  int first = ty * tile_cols + tx;
  uint32_t fb_start = fb + 8 * ty * tile_cols + tx;
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
  else
  {
    fprintf(tests, "%x %x %x %x %x %x %x %x\n", fmt, tile_cols, first, w, h, fb_start, map, clut);
    fprintf(expected, "# %d %d %d %d %d %d\n", t++, fmt, tile_cols, first, w, h);
  }

  memset(&u8x8, 0, sizeof(u8x8));
  for ( i = 0; i < U8X8_CLUT_ENTRY_CNT; i++ )
  {
    u8x8.clut[i][0] = rd16(clut + 4 * i);
    u8x8.clut[i][1] = rd16(clut + 4 * i + 2);
  }
  u8x8.tile_clut_map = mem + map;

  /* memory reads of clut_dma, with the clut index kept over the request */
  {
    int nreads = 0, valid = 0, last_idx = 0, y;

    for ( y = 0; y < h && w > 0; y++ )
      for ( c = 0; c < n; c++ )
      {
        int tile = first + (y / 8) * tile_cols + c;
        uint32_t fbaddr = fb_start + y * tile_cols + c;
        int idx = (mem[map + tile / 2] >> (4 * (tile & 1))) & 15;

        if ( c == 0 || tile % 8 == 0 )
          nreads++;
        if ( !valid || idx != last_idx )
          nreads++;
        valid = 1;
        last_idx = idx;
        if ( c == 0 || fbaddr % 4 == 0 )
          nreads++;
      }
    fprintf(reads, "%d\n", nreads);
  }

  if ( w == 0 || h == 0 )
    return;

  for ( k = 0; k * 8 < h; k++ )
  {
    /* the tiles of this tile row in u8g2 vertical layout */
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

    ncap = 0;
    if ( fmt == F_RGB444 )
      u8x8_clut_stream_rgb444(&u8x8, vt, (uint8_t)n, (uint16_t)(first + k * tile_cols));
    else
      u8x8_clut_stream_rgb565(&u8x8, vt, (uint8_t)n, (uint16_t)(first + k * tile_cols));

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
static void random_request(int fmt)
{
  int tile_cols = 1 + rnd() % 40;
  int tile_rows = MAX_TILES / tile_cols;
  int tx = rnd() % tile_cols;
  int ty = rnd() % tile_rows;
  int w = 1 + rnd() % ((tile_cols - tx) * 8);
  int h = 1 + rnd() % ((tile_rows - ty) * 8);

  if ( fmt == F_RGB444 && (w & 1) )
    w++;
  request(fmt, tile_cols, tx, ty, w, h, FB_A, MAP_A, CLUT_A);
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
  request(F_RGB444, FAB_COLS, 0, 0, 240, 280, FAB_FB, FAB_MAP, FAB_CLUT);      /* whole screen */
  request(F_RGB565, FAB_COLS, 3, 5, 56, 32, FAB_FB, FAB_MAP, FAB_CLUT);        /* rectangle */
  request(F_RGB666, FAB_COLS, 25, 34, 40, 8, FAB_FB, FAB_MAP, FAB_CLUT);       /* bottom right */
  request(F_RGB565, FAB_COLS, 0, 0, 240, 8, FAB_FB, FAB_MAP, FAB_CLUT);        /* top tile row */
  request(F_RGB565, FAB_COLS, 1, 1, 37, 13, FAB_FB, FAB_MAP, FAB_CLUT);        /* odd size */

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

  if ( argc > 1 && strcmp(argv[1], "fabric") == 0 )
    return fabric_main();

  fill_memory();
  if ( !open_files(MEM_SIZE) )
    return 1;

  for ( fmt = F_RGB565; fmt <= F_RGB666; fmt++ )
  {
    int odd = (fmt == F_RGB444) ? 0 : 1;   /* rgb444: even widths */

    if ( narrow )
    {
      request(fmt, 40, 20, 0, 8, 240, FB_A, MAP_A, CLUT_A);         /* 1 tile wide, 320x240 */
      continue;
    }
    if ( uniform )
    {
      request(fmt, 30, 0, 0, 240, 240, FB_A, MAP_B, CLUT_A);        /* whole screen, one index */
      continue;
    }
    if ( measure )
    {
      request(fmt, 30, 0, 0, 240, 240, FB_A, MAP_A, CLUT_A);        /* whole screen, 240x240 */
      continue;
    }
    request(fmt, 30, 0, 0, 240, 8, FB_A, MAP_A, CLUT_A);            /* one tile row, 240 wide */
    request(fmt, 30, 7, 5, 88, 8, FB_A, MAP_A, CLUT_A);             /* run in the middle of a tile row */
    request(fmt, 30, 29, 29, 8, 8, FB_A, MAP_A, CLUT_A);            /* last tile of 240x240 */
    request(fmt, 30, 0, 0, 240, 240, FB_A, MAP_A, CLUT_A);          /* whole screen, 240x240 */
    request(fmt, 30, 4, 10, 72, 48, FB_A, MAP_A, CLUT_A);           /* rectangle */
    request(fmt, 30, 29, 3, 8, 80, FB_A, MAP_A, CLUT_A);            /* one tile column, right edge */
    request(fmt, 30, 20, 12, 80, 40, FB_A, MAP_A, CLUT_A);          /* rectangle at the right edge */
    request(fmt, 40, 0, 0, 320, 240, FB_A, MAP_A, CLUT_A);          /* whole screen, 320x240 */
    request(fmt, 17, 0, 0, 134 + odd, 240, FB_A, MAP_A, CLUT_A);    /* 135x240, odd width */
    request(fmt, 30, 0, 0, 240, 135, FB_A, MAP_A, CLUT_A);          /* 240x135, partial tile row */
    request(fmt, 30, 3, 2, 6 + odd, 3, FB_A, MAP_A, CLUT_A);        /* less than a tile */
    request(fmt, 30, 0, 0, 240, 16, FB_B, MAP_B, CLUT_B);           /* all background, index 5 */
    request(fmt, 30, 0, 0, 240, 16, FB_C, MAP_C, CLUT_B);           /* all foreground, index 0 and 15 */
    request(fmt, 30, 0, 0, 0, 40, FB_A, MAP_A, CLUT_A);             /* empty: w = 0 */
    request(fmt, 30, 0, 0, 40, 0, FB_A, MAP_A, CLUT_A);             /* empty: h = 0 */
    for ( i = 0; i < 20; i++ )
      random_request(fmt);
  }

  fclose(tests);
  fclose(expected);
  fclose(reads);
  return 0;
}
