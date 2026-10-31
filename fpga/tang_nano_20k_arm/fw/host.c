/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  host.c: the input of the simulation of a firmware test, on the pc, from
  testimg.c: fbmem as the firmware fills it, and the bytes and requests the
  firmware sends. For fpga/tb/tb_frame.v. Writes
    mem.hex    fbmem at the start, 4096 words of 32 bits, for $readmemh
    steps.txt  c XX: command, d XX: parameter, w x0 x1 y0 y1: window,
               m word value: fbmem write, before a request that needs it,
               r fmt fb_start map clut line_stride map_nibble size: request,
               fmt bits 1:0 FORMAT, bit 2 VERT, bit 3 NO_MAP
    names.txt  the name of each request
  usage: host [border | palette | clutcolor | clutclock | graphics | mui | stars | solder]
         stars: built with -DWITH_STARS and Adafruit GFX
         solder: built with -DSOLDER_TEST and solder.c, its own fbmem layout
*/

#include <stdio.h>
#include <string.h>
#include "testimg.h"
#include "u8x8.h"

#define FBMEM 0x80000000u           /* the cpu address of fbmem */
#define TICKS 12                    /* clutcolor: seconds after the start screen */
#define CLOCK_TICKS 3               /* clutclock: a whole screen every second */
#define STARS_TICKS 40              /* stars: frames sent per star */

static uint8_t mem[16384], prev[16384];
static FILE *f, *n;
static int quiet;                   /* nothing is sent */
static int framed;                  /* requests make up frames, see frame_end() */

static uint32_t word(const uint8_t *m, unsigned i)
{
  return (uint32_t)m[i + 3] << 24 | (uint32_t)m[i + 2] << 16 | (uint32_t)m[i + 1] << 8 | m[i];
}

/* the words of fbmem that changed since the last request */
static void sync_mem(void)
{
  unsigned i;

  for (i = 0; i < sizeof mem; i += 4)
    if (word(mem, i) != word(prev, i))
      fprintf(f, "m %x %08x\n", i / 4, (unsigned)word(mem, i));
  memcpy(prev, mem, sizeof mem);
}

static void request(unsigned fmt, unsigned fb, unsigned tx, unsigned ty, unsigned w, unsigned h, unsigned clut, const char *name)
{
  unsigned x0 = 8 * tx, y0 = 8 * ty + LCD_YOFS;

  sync_mem();
  fprintf(f, "w %x %x %x %x\n", x0, x0 + w - 1, y0, y0 + h - 1);
  unsigned first = ty * COLS + tx;

  /* FB_START the first tile, MAP_START and MAP_NIBBLE its map entry,
     LINE_STRIDE a pixel row (horizontal) or a tile row (vertical) */
  fprintf(f, "r %x %x %x %x %x %x %x\n", fmt,
          FBMEM + fb, FBMEM + MAP_OFS + first / 2, FBMEM + clut,
          (fmt & FMT_VERT) ? 8 * COLS : COLS, first & 1, h << 16 | w);
  if (name)
    fprintf(n, "%s\n", name);
}

/* the u8x8 byte function of the demos that send through u8g2, as
   u8x8_byte_lcd() in main.c: each byte a c (command) or d (data) step */
static uint8_t dc;

static uint8_t byte_steps(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  const uint8_t *p = (const uint8_t *)arg_ptr;

  (void)u8x8;
  if (msg == U8X8_MSG_BYTE_SET_DC)
    dc = arg_int;
  else if (msg == U8X8_MSG_BYTE_SEND && !quiet)
    while (arg_int--)
      fprintf(f, "%c %02x\n", dc ? 'd' : 'c', *p++);
  return 1;
}

#ifdef USE_CLUT_DMA
/* clut_dma, as u8x8_clut_dma.c: a request for a run of tiles, after the
   window the driver sent. the pointers into mem[] as fbmem addresses */
void u8x8_clut_stream_rgb444(u8x8_t *u8x8, const uint8_t *run_tile_ptr, uint8_t cnt, uint16_t first_tile_number)
{
  unsigned fmt = FMT_RGB444 | FMT_VERT, map = 0;

  if (quiet)
    return;
  if (u8x8->tile_clut_map)
    map = FBMEM + (unsigned)(u8x8->tile_clut_map - mem) + first_tile_number / 2u;
  else
    fmt |= FMT_NOMAP;
  sync_mem();
  fprintf(f, "r %x %x %x %x %x %x %x\n", fmt, FBMEM + (unsigned)(run_tile_ptr - mem), map,
          FBMEM + (unsigned)((const uint8_t *)u8x8->clut - mem),
          8u * u8x8->display_info->tile_width, first_tile_number & 1u, 8u << 16 | 8u * cnt);
  if (!framed)
    fprintf(n, "tile row\n");
}
#endif

#ifdef WITH_STARS
/* a display update of the Adafruit demo, as send_row() in main.c */
static void send_row(uint8_t tx, uint8_t ty, uint16_t w, uint16_t h, const char *what)
{
  request(FMT_RGB444, FB_OFS + 8u * ty * COLS + tx, tx, ty, w, h, CLUT_OFS, framed ? 0 : what);
}
#endif

/* the end of a frame of several requests: st7789.py draws a picture here */
static void frame_end(const char *name)
{
  fprintf(f, "f\n");
  fprintf(n, "%s\n", name);
}

int main(int argc, char **argv)
{
  const char *test = argc > 1 ? argv[1] : "";
  const struct step *s = 0;
  unsigned i;

  f = fopen("mem.hex", "w");
  if (!f)
    return 1;
  for (i = 0; i < sizeof mem; i += 4)
    fprintf(f, "%08x\n", (unsigned)word(mem, i));
  fclose(f);

  f = fopen("steps.txt", "w");
  n = fopen("names.txt", "w");
  if (!f || !n)
    return 1;
  /* as lcd_init() in main.c, after reset and sleep out */
  fprintf(f, "c 3a\nd 05\nc 36\nd %02x\n", (unsigned)LCD_MADCTL);

  if (0) {
#ifdef SOLDER_TEST
  } else if (strcmp(test, "solder") == 0) {
    /* the home screen cold, heating after 2 s, holding after 20 s, with a
       sensor fault; the menu, the set temperature up by 2 steps */
    static const unsigned at[] = { 20, 200 };
    unsigned t = 0, k;
    fprintf(f, "c 3a\nd 03\n");
    framed = 1;
    solder_init(mem, byte_steps);
    frame_end("solder: cold");
    for (k = 0; k < sizeof at / sizeof at[0]; k++) {
      quiet = 1;
      while (++t < at[k])
        solder_tick(1);
      quiet = 0;
      solder_tick(1);
      frame_end(k == 0 ? "solder: heating" : "solder: holding");
    }
    solder_key('e');
    frame_end("solder: sensor fault");
    quiet = 1;
    solder_key('e');
    solder_key(' ');
    quiet = 0;
    solder_key('d');
    frame_end("solder: menu, set temperature +10");
#endif
  } else if (strcmp(test, "mui") == 0) {
    /* the menu; Enter Data with number 5, bar 8, animal 3 (Crow); Show Data */
    static const char enter[] = " dddddsddddddddsddd", show[] = "s s ";
    fprintf(f, "c 3a\nd 03\n");
    framed = 1;
    mui_demo_init(mem, byte_steps);
    frame_end("frame");
    for (i = 0; enter[i]; i++) {
      quiet = enter[i + 1] != 0;
      mui_demo_key(enter[i]);
    }
    frame_end("frame");
    for (i = 0; show[i]; i++) {
      quiet = show[i + 1] != 0;
      mui_demo_key(show[i]);
    }
    frame_end("frame");
  } else if (strcmp(test, "graphics") == 0) {
    /* a frame of each page, frame 3 of the animated ones */
    fprintf(f, "c 3a\nd 03\n");
    framed = 1;
    graphics_init(mem, byte_steps);
    for (i = 0; i < GRAPHICS_PAGES; i++) {
      graphics_page((uint8_t)i, 3);
      frame_end("frame");
    }
  } else if (strcmp(test, "clutclock") == 0) {
    fprintf(f, "c 3a\nd 03\n");
    framed = 1;
    clutclock_init(mem, byte_steps);
    frame_end("clock");
    for (i = 0; i < CLOCK_TICKS; i++) {
      clutclock_tick();
      frame_end("clock");
    }
#ifdef WITH_STARS
  } else if (strcmp(test, "stars") == 0) {
    /* frames of falling stars, sent per star; then the same image as a
       whole screen: the last two frames are the same */
    fprintf(f, "c 3a\nd 03\n");
    framed = 1;
    stars_init(mem, send_row);
    frame_end("stars");
    for (i = 0; i < STARS_TICKS; i++) {
      stars_tick(mem, send_row, 0);
      frame_end("stars");
    }
    send_row(0, 0, LCD_W, LCD_H, 0);
    frame_end("stars, whole screen");
#endif
  } else if (strcmp(test, "clutcolor") == 0) {
    fprintf(f, "c 3a\nd 03\n");
    framed = 1;
    clutcolor_init(mem, byte_steps);
    frame_end("clutcolor");
    for (i = 0; i < TICKS; i++) {
      clutcolor_tick();
      frame_end("clutcolor");
    }
  } else if (strcmp(test, "palette") == 0) {
    fprintf(f, "c 3a\nd 03\n");
    framed = 1;
    palette(mem, byte_steps);
    frame_end("palette");
  } else {
    if (strcmp(test, "border") == 0) {
      border(mem);
      s = border_steps;
    } else {
      pattern(mem);
      s = pattern_steps;
    }
    for (; s->kind != STEP_END; s++)
      switch (s->kind) {
      case STEP_REQ:
        request(s->fmt, (s->fmt & FMT_VERT) ? FB_OFS + 8 * (s->ty * COLS + s->tx)
                                            : FB_OFS + 8 * s->ty * COLS + s->tx,
                s->tx, s->ty, s->w, s->h, s->clut, s->name);
        break;
      case STEP_COLMOD:
        fprintf(f, "c 3a\nd %02x\n", s->fmt);
        break;
      default:
        break;
      }
  }
  fclose(f);
  fclose(n);
  return 0;
}
