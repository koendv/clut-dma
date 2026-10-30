/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  host.c: the input of the simulation of a firmware test, on the pc, from
  testimg.c: fbmem as the firmware fills it, and the bytes and requests the
  firmware sends. For fpga/tb/tb_frame.v. Writes
    mem.hex    fbmem at the start, 4096 words of 32 bits, for $readmemh
    steps.txt  c XX: command, d XX: parameter, w x0 x1 y0 y1: window,
               m word value: fbmem write, before a request that needs it,
               r fmt fb_start map clut line_offset first size: request
    names.txt  the name of each request
  usage: host [border | palette | clutcolor | clutclock | graphics | mui]
*/

#include <stdio.h>
#include <string.h>
#include "testimg.h"

#define FBMEM 0x80000000u           /* the cpu address of fbmem */
#define TICKS 12                    /* clutcolor: seconds after the start screen */
#define CLOCK_TICKS 3               /* clutclock: a whole screen every second */

static uint8_t mem[16384], prev[16384];
static FILE *f, *n;
static int quiet;                   /* send_rect() sends nothing */
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
  fprintf(f, "r %x %x %x %x %x %x %x\n", fmt,
          FBMEM + fb, FBMEM + MAP_OFS, FBMEM + clut,
          COLS - (w + 7) / 8, ty * COLS + tx, h << 16 | w);
  if (name)
    fprintf(n, "%s\n", name);
}

/* a display update of a demo, as send_rect() in main.c */
static void send_rect(uint16_t fb, uint8_t tx, uint8_t ty, uint8_t tw, uint8_t th, const char *what)
{
  if (quiet)
    return;
  request(FMT_RGB444, fb, tx, ty, tw * 8u, th * 8u, CLUT_OFS, framed ? 0 : what);
}

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

  if (strcmp(test, "mui") == 0) {
    /* the menu; Enter Data with number 5, bar 8, animal 3 (Crow); Show Data */
    static const char enter[] = " dddddsddddddddsddd", show[] = "s s ";
    fprintf(f, "c 3a\nd 03\n");
    mui_demo_init(mem, send_rect);
    for (i = 0; enter[i]; i++) {
      quiet = enter[i + 1] != 0;
      mui_demo_key(mem, send_rect, enter[i]);
    }
    for (i = 0; show[i]; i++) {
      quiet = show[i + 1] != 0;
      mui_demo_key(mem, send_rect, show[i]);
    }
  } else if (strcmp(test, "graphics") == 0) {
    /* a frame of each page, frame 3 of the animated ones */
    fprintf(f, "c 3a\nd 03\n");
    framed = 1;
    graphics_init(mem);
    for (i = 0; i < GRAPHICS_PAGES; i++) {
      graphics_page(mem, send_rect, (uint8_t)i, 3);
      frame_end("frame");
    }
  } else if (strcmp(test, "clutclock") == 0) {
    fprintf(f, "c 3a\nd 03\n");
    framed = 1;
    clutclock_init(mem, send_rect);
    frame_end("clock");
    for (i = 0; i < CLOCK_TICKS; i++) {
      clutclock_tick(mem, send_rect);
      frame_end("clock");
    }
  } else if (strcmp(test, "clutcolor") == 0) {
    fprintf(f, "c 3a\nd 03\n");
    clutcolor_init(mem, send_rect);
    for (i = 0; i < TICKS; i++)
      clutcolor_tick(mem, send_rect);
  } else {
    if (strcmp(test, "border") == 0) {
      border(mem);
      s = border_steps;
    } else if (strcmp(test, "palette") == 0) {
      palette(mem);
      s = palette_steps;
    } else {
      pattern(mem);
      s = pattern_steps;
    }
    for (; s->kind != STEP_END; s++)
      switch (s->kind) {
      case STEP_REQ:
        request(s->fmt, FB_OFS + 8 * s->ty * COLS + s->tx, s->tx, s->ty, s->w, s->h, s->clut, s->name);
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
