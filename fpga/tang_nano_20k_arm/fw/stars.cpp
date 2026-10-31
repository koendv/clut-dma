/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  stars.cpp: falling stars, with Adafruit GFX, as testanimate() in the
  ssd1306_128x64_i2c example of Adafruit_SSD1306, in color. The drawing is
  Adafruit GFX on a GFXcanvas1, 1 bit per pixel in the horizontal layout,
  in fbmem; next to it a tile map and a clut, and clut_dma makes the colors
  on the way to the display. A star has a color; where two stars share an
  8x8 tile, the tile has the color of one of them.
  Shared by the firmware (main.c) and the simulation (host.c).
*/

#include <new>
#include <Adafruit_GFX.h>
#include "testimg.h"

/* GFXcanvas1 with its bitmap in memory clut_dma reads, a tile map and a
   clut: a bitmap row is (w + 7) / 8 bytes, the leftmost pixel in bit 7, as
   clut_dma reads it without VERT. A set bit is foreground. */
class ClutCanvas : public GFXcanvas1 {
public:
  ClutCanvas(uint8_t *bitmap, uint8_t *map, uint32_t *clut, uint16_t w, uint16_t h)
      : GFXcanvas1(w, h, false), map_(map), clut_(clut)
  {
    buffer = bitmap;
  }

  /* the clut index of the tiles under a rectangle of pixels */
  void setTileColor(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t idx)
  {
    int16_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int16_t x1 = x + w > WIDTH ? WIDTH : x + w, y1 = y + h > HEIGHT ? HEIGHT : y + h;
    uint16_t cols = (WIDTH + 7) / 8;

    if (x0 >= x1 || y0 >= y1)
      return;
    for (int16_t ty = y0 / 8; ty <= (y1 - 1) / 8; ty++)
      for (int16_t tx = x0 / 8; tx <= (x1 - 1) / 8; tx++) {
        uint16_t t = ty * cols + tx;
        uint8_t *b = &map_[t / 2];
        *b = (t & 1) ? (uint8_t)((*b & 0x0F) | idx << 4) : (uint8_t)((*b & 0xF0) | idx);
      }
  }

  /* clut entry idx: foreground and background, in the format of the display */
  void setClut(uint8_t idx, uint16_t fg, uint16_t bg) { clut_[idx] = (uint32_t)bg << 16 | fg; }

private:
  uint8_t *map_;
  uint32_t *clut_;
};

#define NSTARS 24
#define STAR   16                /* a star is 16 x 16 pixels */

/* a five-pointed star, 16 x 16, a row of 2 bytes, the leftmost pixel in bit 7 */
static const uint8_t star_bmp[2 * STAR] = {
  0x01, 0x80, /* .......##....... */
  0x01, 0x80, /* .......##....... */
  0x03, 0xC0, /* ......####...... */
  0x03, 0xC0, /* ......####...... */
  0x07, 0xE0, /* .....######..... */
  0xFF, 0xFF, /* ################ */
  0x7F, 0xFE, /* .##############. */
  0x1F, 0xF8, /* ...##########... */
  0x0F, 0xF0, /* ....########.... */
  0x0F, 0xF0, /* ....########.... */
  0x1F, 0xF8, /* ...##########... */
  0x1E, 0x78, /* ...####..####... */
  0x3C, 0x3C, /* ..####....####.. */
  0x38, 0x1C, /* ..###......###.. */
  0x60, 0x06, /* .##..........##. */
  0x40, 0x02, /* .#............#. */
};

/* the colors of the stars, rgb444, clut entries 1 to 15 on black */
static const uint16_t star_color[15] = {
  0xF00, 0xF60, 0xFF0, 0x9F0, 0x0F0, 0x0F9, 0x0FF, 0x09F,
  0x66F, 0x96F, 0xF0F, 0xF09, 0xFFF, 0xFC9, 0x9CF,
};

struct star {
  int16_t x, y;                  /* top left */
  uint8_t v;                     /* pixels per frame */
  uint8_t idx;                   /* clut entry */
};

static star stars[NSTARS];
alignas(ClutCanvas) static uint8_t canvas_mem[sizeof(ClutCanvas)];
static ClutCanvas *canvas;
static uint32_t rnd_state = 2463534242u;

/* xorshift32: the same stars on the board and in the simulation */
static uint32_t rnd(uint32_t n)
{
  rnd_state ^= rnd_state << 13;
  rnd_state ^= rnd_state >> 17;
  rnd_state ^= rnd_state << 5;
  return rnd_state % n;
}

static void place(star *s, int16_t y)
{
  s->x = (int16_t)rnd(LCD_W - STAR + 1);
  s->y = y;
  s->v = (uint8_t)(1 + rnd(4));
}

static void draw(const star *s)
{
  canvas->drawBitmap(s->x, s->y, star_bmp, STAR, STAR, 1);
  canvas->setTileColor(s->x, s->y, STAR, STAR, s->idx);
}

/* send pixel rows y0 to y1 - 1 of the tiles under a star at x: the top left
   on a tile boundary, the width up to the last tile of the star */
static void send_rows(row_send send, int16_t x, int16_t y0, int16_t y1)
{
  if (y0 < 0)
    y0 = 0;
  if (y1 > (int16_t)LCD_H)
    y1 = LCD_H;
  if (y0 >= y1)
    return;
  uint8_t tx = (uint8_t)(x / 8), ty = (uint8_t)(y0 / 8);
  uint16_t end = (uint16_t)((x + STAR + 7) / 8);    /* the tile after the star */

  if (end > COLS)
    end = COLS;
  send(tx, ty, (uint16_t)((end - tx) * 8), (uint16_t)(y1 - 8 * ty), "star");
}

void stars_init(volatile uint8_t *mem, row_send send)
{
  uint8_t *m = (uint8_t *)(uintptr_t)mem;
  unsigned i;

  canvas = new (canvas_mem) ClutCanvas(m + FB_OFS, m + MAP_OFS, (uint32_t *)(void *)(m + CLUT_OFS), LCD_W, LCD_H);
  canvas->setClut(0, 0xFFF, 0x000);
  for (i = 1; i < 16; i++)
    canvas->setClut((uint8_t)i, star_color[i - 1], 0x000);
  canvas->fillScreen(0);
  canvas->setTileColor(0, 0, LCD_W, LCD_H, 0);
  for (i = 0; i < NSTARS; i++) {
    place(&stars[i], (int16_t)rnd(LCD_H + STAR) - STAR);
    stars[i].idx = (uint8_t)(1 + i % 15);
    draw(&stars[i]);
  }
  send(0, 0, LCD_W, LCD_H, "stars");
}

/* a frame: the stars move down; a star below the screen starts again above
   it. full: send the whole screen, else the tiles each star left and entered */
void stars_tick(volatile uint8_t *mem, row_send send, int full)
{
  star old[NSTARS];
  unsigned i;

  (void)mem;
  /* clear all, then draw all: a star drawn over by another stays whole */
  for (i = 0; i < NSTARS; i++) {
    star *s = &stars[i];

    old[i] = *s;
    canvas->fillRect(s->x, s->y, STAR, STAR, 0);
    canvas->setTileColor(s->x, s->y, STAR, STAR, 0);
    s->y = (int16_t)(s->y + s->v);
    if (s->y >= (int16_t)LCD_H)
      place(s, -STAR);
  }
  for (i = 0; i < NSTARS; i++)
    draw(&stars[i]);

  if (full) {
    send(0, 0, LCD_W, LCD_H, "screen");
    return;
  }
  for (i = 0; i < NSTARS; i++) {
    const star *o = &old[i], *s = &stars[i];

    if (s->x == o->x && s->y > o->y) {
      send_rows(send, s->x, o->y, s->y + STAR);
    } else {
      send_rows(send, o->x, o->y, o->y + STAR);
      send_rows(send, s->x, s->y, s->y + STAR);
    }
  }
}

/* the c++ runtime without libstdc++: no exceptions, no heap. GFXcanvas1 is
   made with its buffer, so its malloc() is never called */
extern "C" void __cxa_pure_virtual(void)
{
  for (;;)
    ;
}

void operator delete(void *p) noexcept { (void)p; }
void operator delete(void *p, size_t n) noexcept { (void)p; (void)n; }

#ifdef __arm__
extern "C" void *_sbrk(ptrdiff_t n)
{
  (void)n;
  return (void *)-1;
}
#endif
