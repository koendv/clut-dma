/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Koen De Vleeschauwer */

/*
  testimg.h: test images and request sequences, shared by the firmware
  (main.c) and the simulation (host.c, fpga/tb/tb_frame.v): both send the
  same bytes to the display.
*/

#ifndef TESTIMG_H
#define TESTIMG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* display: 240x280 window at row 20 of the 240x320 ram, as in u8g2 */
#define LCD_W      240u
#define LCD_H      280u
#define LCD_YOFS   20u
#ifndef LCD_MADCTL
#define LCD_MADCTL 0x00u          /* connector at the top; 0xC0 (MY, MX): at the bottom */
#endif

#define COLS (LCD_W / 8)           /* TILE_COLS, bitmap bytes per pixel row */
#define ROWS (LCD_H / 8)

/* fbmem layout, offsets from the start of fbmem */
#define FB_OFS    0x0000u          /* 30 * 280 = 8400 bytes */
#define MAP_OFS   0x2100u          /* 30 * 35 nibbles = 525 bytes */
#define CLUT_OFS  0x2340u          /* rgb565 */
#define CLUT2_OFS 0x2380u          /* rgb565, fg and bg swapped */
#define CLUT4_OFS 0x23C0u          /* rgb444 */
#define CLUT3_OFS 0x2400u          /* rgb565, border test */

/* clut_dma FORMAT */
#define FMT_RGB565 0u
#define FMT_RGB444 1u
#define FMT_RGB666 2u
/* clut_dma CTRL VERT, here in bit 2 of the format: the bitmap in the
   vertical tile layout of u8g2, FB_START the address of the first tile */
#define FMT_VERT   4u

/* the u8g2 demos: the vertical layout of u8g2, rgb444 */
#define DEMO_FMT (FMT_RGB444 | FMT_VERT)

/* a step of a test. REQ: a clut_dma request of w x h pixels at tile (tx, ty)
   in format fmt, with the clut at offset clut. COLMOD: the display color
   format fmt. DELAY: w ms, only on the hardware */
enum { STEP_END, STEP_REQ, STEP_COLMOD, STEP_DELAY };

struct step {
  uint8_t     kind, fmt, tx, ty;
  uint16_t    w, h, clut;
  const char *name;
};

void pattern(volatile uint8_t *mem);
void border(volatile uint8_t *mem);
void palette(volatile uint8_t *mem);

/* dynamic demos: they ask for display updates as u8g2 updateDisplayArea()
   would, a rectangle of tiles, in rgb444 with the clut at CLUT_OFS, the
   bitmap in the vertical layout of u8g2. fb: the fbmem offset of the first
   tile of the rectangle */
typedef void (*demo_send)(uint32_t fb, uint8_t tx, uint8_t ty, uint8_t tw, uint8_t th, const char *what);

/* tile rows of the u8g2 buffer of ClutClock, demo_graphics and demo_mui: ROWS for a full
   buffer, 1 or 2 for the page buffer modes of u8g2 */
#ifndef PAGE_ROWS
#define PAGE_ROWS ROWS
#endif

void clutcolor_init(volatile uint8_t *mem, demo_send send);
void clutcolor_tick(volatile uint8_t *mem, demo_send send);   /* every second */
void clutclock_init(volatile uint8_t *mem, demo_send send);
void clutclock_tick(volatile uint8_t *mem, demo_send send);   /* every second */
void graphics_init(volatile uint8_t *mem);
void graphics_page(volatile uint8_t *mem, demo_send send, uint8_t page, uint8_t a);
uint16_t graphics_step(volatile uint8_t *mem, demo_send send); /* returns the pause in ms */
#define GRAPHICS_PAGES 11
void mui_demo_init(volatile uint8_t *mem, demo_send send);
void mui_demo_key(volatile uint8_t *mem, demo_send send, char key);   /* w s a d, space */

extern const struct step pattern_steps[], border_steps[], palette_steps[];

/* the Adafruit GFX demo, stars.cpp: the bitmap in the horizontal layout of
   GFXcanvas1, a pixel row of COLS bytes. an update is a rectangle of w x h
   pixels, top left at tile (tx, ty), in rgb444 with the clut at CLUT_OFS */
typedef void (*row_send)(uint8_t tx, uint8_t ty, uint16_t w, uint16_t h, const char *what);

void stars_init(volatile uint8_t *mem, row_send send);
void stars_tick(volatile uint8_t *mem, row_send send, int full);   /* a frame */

#ifdef __cplusplus
}
#endif

#endif
