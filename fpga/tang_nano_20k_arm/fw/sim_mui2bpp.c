/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  sim_mui2bpp.c: the menu of mui_2bpp (mui2bpp.c) for the simulation, on the
  pc: fbmem as the firmware fills it, and what it sends. For
  fpga/tb/tb_frame.v, as host.c. Writes
    mem.hex    fbmem at the start, 4096 words of 32 bits: zero
    steps.txt  c XX, d XX: the bytes of the u8x8 driver; with USE_CLUT_DMA
               m word value: the fbmem words that changed, and r ...: a
               request with BPP2 for each tile row; without it the cpu
               expands the colors (mui_2bpp/src/u8x8_clut_2bpp.c) and the
               pixels are d steps. f: the end of a screen.
    names.txt  the key of each screen
  Both ways the bytes that reach the display (spi.txt) must be the same.
*/

#include <stdio.h>
#include <string.h>
#include "mui2bpp.h"
#include "menu.h"

#define FBMEM 0x80000000u         /* the cpu address of fbmem */
#define FMT_RGB444 1u
#define FMT_NOMAP  8u
#define FMT_BPP2   16u

static uint8_t mem[16384], prev[16384];
static FILE *f, *n;
static uint8_t dc;

static uint8_t byte_steps(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  const uint8_t *p = (const uint8_t *)arg_ptr;

  (void)u8x8;
  if (msg == U8X8_MSG_BYTE_SET_DC)
    dc = arg_int;
  else if (msg == U8X8_MSG_BYTE_SEND)
    while (arg_int--)
      fprintf(f, "%c %02x\n", dc ? 'd' : 'c', *p++);
  return 1;
}

static uint8_t gpio_none(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
  (void)u8x8;
  (void)msg;
  (void)arg_int;
  (void)arg_ptr;
  return 1;
}

#ifdef USE_CLUT_DMA
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

/* clut_dma, as u8x8_clut_dma_2bpp.c: a request for a run of tiles, after the
   window the driver sent; the pointers into mem[] as fbmem addresses */
void u8x8_clut_stream_rgb444(u8x8_t *u8x8, const uint8_t *run_tile_ptr, uint8_t cnt, uint16_t first_tile_number)
{
  unsigned fmt = FMT_RGB444 | FMT_BPP2, map = 0;

  if (u8x8->tile_clut_map && first_tile_number < U8G2_TILE_COLS * U8G2_TILE_ROWS)
    map = FBMEM + (unsigned)(u8x8->tile_clut_map - mem) + first_tile_number / 2u;
  else
    fmt |= FMT_NOMAP;
  sync_mem();
  fprintf(f, "r %x %x %x %x %x %x %x\n", fmt, FBMEM + (unsigned)(run_tile_ptr - mem), map,
          FBMEM + (unsigned)((const uint8_t *)u8g2_GetClutTable2bpp(u8x8) - mem),
          (unsigned)U8G2_LINE_STRIDE, first_tile_number & 1u, 8u << 16 | 8u * cnt);
}

void u8x8_clut_set_native_bpp(u8x8_t *u8x8, uint8_t native_bpp)
{
  u8x8->clut_native_bpp = native_bpp;
}
#endif

static const struct {
  uint8_t event;
  const char *name;
} keys[] = {
  { MENU_NEXT,   "next: values" },
  { MENU_SELECT, "select: the values form" },
  { MENU_NEXT,   "next: baud rate" },
  { MENU_SELECT, "select: edit the baud rate" },
  { MENU_NEXT,   "next: 38400" },
  { MENU_SELECT, "select: baud rate done" },
  { MENU_NEXT,   "next: 1 stop bit" },
  { MENU_NEXT,   "next: 2 stop bits" },
};

static void screen(const char *name)
{
  fprintf(f, "f\n");
  fprintf(n, "%s\n", name);
}

int main(void)
{
  u8g2_t u8g2;
  unsigned i;

  f = fopen("mem.hex", "w");
  for (i = 0; i < sizeof mem / 4; i++)
    fprintf(f, "00000000\n");
  fclose(f);
  f = fopen("steps.txt", "w");
  n = fopen("names.txt", "w");

  u8g2_Setup_st7789_240x280(&u8g2, mem + MUI2_FB_OFS, byte_steps, gpio_none);
  u8g2_InitDisplay(&u8g2);
  u8g2_SetFlipMode(&u8g2, 1);
  menu_init(&u8g2, mem + MUI2_MAP_OFS, (uint32_t (*)[2])(void *)(mem + MUI2_CLUT_OFS));
  u8g2_ClearDisplay(&u8g2);
  screen("clear display");
  menu_draw(&u8g2);
  screen("start: main menu");
  for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
    menu_event(keys[i].event);
    menu_draw(&u8g2);
    screen(keys[i].name);
  }
  fclose(f);
  fclose(n);
  return 0;
}
